#include <SDL2/SDL.h>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include "ImGuiFileDialog.h"

#include "FourierCore.hpp"
#include "SVGParser.hpp"
#include "Renderer.hpp"
#include "VideoExporter.hpp"

#include <memory>
#include <thread>
#include <atomic>
#include <future>

// --- Helper: HSV to RGB ---
glm::vec4 HSVtoRGB(float h, float s, float v, float a) {
    int i = (int)(h * 6);
    float f = h * 6 - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);
    switch (i % 6) {
        case 0: return glm::vec4(v, t, p, a);
        case 1: return glm::vec4(q, v, p, a);
        case 2: return glm::vec4(p, v, t, a);
        case 3: return glm::vec4(p, q, v, a);
        case 4: return glm::vec4(t, p, v, a);
        case 5: return glm::vec4(v, p, q, a);
    }
    return glm::vec4(1, 1, 1, a);
}

// --- Helper: Screen Quad ---
class ScreenQuad {
    GLuint vao, vbo;
    Shader shader;
public:
    ScreenQuad() : shader(
        R"(#version 330 core
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTexCoords;
        out vec2 TexCoords;
        void main() {
            gl_Position = vec4(aPos.x, aPos.y, 0.0, 1.0); 
            TexCoords = aTexCoords;
        })",
        R"(#version 330 core
        out vec4 FragColor;
        in vec2 TexCoords;
        uniform sampler2D screenTexture;
        void main() {
            FragColor = texture(screenTexture, TexCoords);
        })"
    ) {
        float quadVertices[] = { 
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    void Draw(GLuint textureID) {
        shader.Use();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textureID);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }
};

// --- FBO Wrapper ---
struct Framebuffer {
    GLuint fbo, tex, rbo;
    int w, h;
    Framebuffer(int width, int height) : w(width), h(height) {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        glGenRenderbuffers(1, &rbo);
        glBindRenderbuffer(GL_RENDERBUFFER, rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    ~Framebuffer() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &tex);
        glDeleteRenderbuffers(1, &rbo);
    }
    void Bind() { glBindFramebuffer(GL_FRAMEBUFFER, fbo); glViewport(0, 0, w, h); }
    void Unbind(int sw, int sh) { glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, sw, sh); }
};

// --- Shaders ---
const char* vShaderCircle = R"(#version 330 core
layout (location = 0) in vec4 aPosUV; layout (location = 1) in vec2 aCenter; layout (location = 2) in float aRadius;
uniform mat4 uProjection; uniform mat4 uView; out vec2 vUV; out float vRadius;
void main() { vRadius = aRadius; vec2 worldPos = aCenter + (aPosUV.xy * aRadius); gl_Position = uProjection * uView * vec4(worldPos, 0.0, 1.0); vUV = aPosUV.zw; })";
const char* fShaderCircle = R"(#version 330 core
in vec2 vUV; in float vRadius; out vec4 FragColor; uniform vec4 uColor;
void main() { float dist = length(vUV); if (dist > 1.0) discard;
    float px = fwidth(dist); float alpha = 1.0 - smoothstep(1.0 - px, 1.0, dist);
    float inner = smoothstep(1.0 - (1.5*px) - px, 1.0 - (1.5*px), dist);
    FragColor = vec4(uColor.rgb, uColor.a * alpha * inner); })";
const char* vShaderLine = R"(#version 330 core
layout (location = 0) in vec2 aPos; uniform mat4 uProjection; uniform mat4 uView;
void main() { gl_Position = uProjection * uView * vec4(aPos, 0.0, 1.0); })";
const char* fShaderLine = R"(#version 330 core
out vec4 FragColor; uniform vec4 uColor; void main() { FragColor = uColor; })";

// --- Async Loader ---
struct LoadedData {
    std::vector<glm::vec2> points;
    std::vector<Epicycle> epis;
};
std::atomic<bool> isLoading{false};
std::future<LoadedData> loadingFuture;
std::string statusMessage = "Ready. Load an SVG to begin.";

void AsyncLoad(std::string path) {
    isLoading = true;
    statusMessage = "Parsing SVG...";
    loadingFuture = std::async(std::launch::async, [path]() {
        LoadedData data;
        // UPDATED: Increased to 10000 for ultra-smooth paths
        data.points = SVGParser::LoadAndSample(path, 10000); 
        if (!data.points.empty()) {
            data.epis = FourierTransform::ComputeDFT(data.points);
        }
        return data;
    });
}

// --- Main ---
int main(int argc, char* argv[]) {
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    
    SDL_Window* window = SDL_CreateWindow("Fourier Forge", 
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, 
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    
    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    glewInit();
    SDL_GL_SetSwapInterval(0); 

    ImGui::CreateContext();
    ImGui_ImplSDL2_InitForOpenGL(window, glContext);
    ImGui_ImplOpenGL3_Init("#version 330");

    const int RENDER_W = 1920;
    const int RENDER_H = 1080;
    Framebuffer fbo(RENDER_W, RENDER_H);
    ScreenQuad screenQuad;

    Shader circleShader(vShaderCircle, fShaderCircle);
    Shader lineShader(vShaderLine, fShaderLine);
    // Increased buffer sizes to handle 10k vectors
    CircleBatch circleBatch(20000);
    LineBatch armBatch(20000);
    TrailRenderer trailRenderer(100000); 
    TrailRenderer pathRenderer(50000);

    std::vector<Epicycle> epicycles;
    std::vector<glm::vec2> pathPoints;
    std::vector<glm::vec2> trail;
    std::vector<glm::vec2> currentCenters;
    std::vector<float> currentRadii;
    std::vector<glm::vec2> armSegments;

    // Animation State
    float time = 0.0f;
    float speed = 0.05f; 
    bool paused = false;
    
    // Camera State
    float zoom = 1.0f;
    glm::vec2 pan(0.0f, 0.0f);
    bool autoFollow = false;
    bool isDragging = false;

    // Visual Settings
    bool showCircles = true, showArms = true, showTrail = true, showRef = false;
    int activeCircles = 10000; 
    bool rainbowMode = false;
    int trailLength = 0;
    float hue = 0.0f;
    glm::vec4 bgColor = glm::vec4(0.05f, 0.05f, 0.1f, 1.0f);
    glm::vec4 inkColor = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
    float strokeWidth = 2.0f;

    // Cinematic State
    bool cinematicMode = false;
    float cinematicMaxZoom = 15.0f;

    std::unique_ptr<VideoExporter> exporter;
    bool recording = false;
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_MOUSEWHEEL && !ImGui::GetIO().WantCaptureMouse) {
                zoom *= (event.wheel.y > 0) ? 1.1f : 0.9f;
            }
            if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT && !ImGui::GetIO().WantCaptureMouse) {
                isDragging = true;
                autoFollow = false; 
                cinematicMode = false;
            }
            if (event.type == SDL_MOUSEBUTTONUP) isDragging = false;
            if (event.type == SDL_MOUSEMOTION && isDragging) {
                pan.x += event.motion.xrel / zoom * (float)RENDER_W / 1280.0f; 
                pan.y -= event.motion.yrel / zoom * (float)RENDER_H / 720.0f;
            }
        }

        // --- Loading Logic ---
        if (isLoading && loadingFuture.valid()) {
            if (loadingFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                LoadedData data = loadingFuture.get();
                if (!data.points.empty()) {
                    pathPoints = data.points;
                    epicycles = data.epis;
                    trail.clear();
                    time = 0.0f;
                    zoom = 1.0f;
                    pan = glm::vec2(0.0f, 0.0f);
                    activeCircles = (int)epicycles.size();
                    statusMessage = "Loaded " + std::to_string(epicycles.size()) + " cycles.";
                    
                    // Auto-reset view logic
                    showRef = true; 
                    showCircles = false; 
                    showArms = false; 
                    showTrail = false;
                    paused = true;
                } else {
                    statusMessage = "Failed: Empty or invalid SVG.";
                }
                isLoading = false;
            } else {
                statusMessage = "Calculating DFT... ";
            }
        }

        // --- Update Visuals ---
        if (rainbowMode) {
            hue += 0.002f;
            if (hue > 1.0f) hue -= 1.0f;
            inkColor = HSVtoRGB(hue, 1.0f, 1.0f, 1.0f);
        }

        // --- Cinematic State Machine ---
        if (cinematicMode && !epicycles.empty()) {
            autoFollow = true;
            if (time < 0.1f) {
                float t = time / 0.1f;
                zoom = glm::mix(1.0f, cinematicMaxZoom, t * t * (3 - 2 * t));
            } 
            else if (time < 0.85f) {
                zoom = cinematicMaxZoom;
            } 
            else {
                float t = (time - 0.85f) / 0.15f;
                zoom = glm::mix(cinematicMaxZoom, 1.0f, t * t * (3 - 2 * t));
                pan = glm::mix(pan, glm::vec2(0,0), 0.05f);
                autoFollow = false;
            }
        }

        // --- Physics Loop ---
        if (!epicycles.empty() && !isLoading) {
            currentCenters.clear(); currentRadii.clear(); armSegments.clear();
            if (activeCircles > (int)epicycles.size()) activeCircles = (int)epicycles.size();
            if (activeCircles < 1) activeCircles = 1;

            int subSteps = recording ? 1 : 5; 
            
            for (int s = 0; s < subSteps; ++s) {
                if (!paused) {
                    if (recording) {
                        time += speed * (1.0f / 60.0f);
                    } else {
                        time += (speed * 0.002f) / subSteps;
                    }

                    if (time >= 1.0f) { 
                        time -= 1.0f; 
                        if (trailLength == 0) trail.clear(); 
                        
                        // CINEMATIC FINISH TRIGGER
                        if (cinematicMode && recording) {
                            recording = false;
                            cinematicMode = false;
                            exporter.reset(); // Saves file
                            paused = true;    // Stop animation
                            time = 0.999f;    // Show finished drawing
                            zoom = 1.0f;      
                            pan = glm::vec2(0,0);
                            statusMessage = "Cinematic Shot Saved Successfully!";
                        }
                    }
                }
                
                std::complex<double> tipPos(0,0);
                for(int i=0; i<activeCircles; ++i) {
                    tipPos += epicycles[i].evaluate(time);
                }
                glm::vec2 tip(tipPos.real(), tipPos.imag());
                
                if (autoFollow) pan = -tip;

                if (!paused) {
                    if (trail.empty() || glm::distance(trail.back(), tip) > 0.5f) {
                        trail.push_back(tip);
                    }
                    if (trailLength > 0 && trail.size() > (size_t)trailLength) {
                        trail.erase(trail.begin());
                    }
                }
            }
            
            // Build Geometry
            std::complex<double> currentPos(0,0);
            for(int i=0; i<activeCircles; ++i) {
                const auto& epi = epicycles[i];
                glm::vec2 prevPos(currentPos.real(), currentPos.imag());
                currentPos += epi.evaluate(time);
                glm::vec2 newPos(currentPos.real(), currentPos.imag());
                
                if (activeCircles < 50 || epi.amp > 1.0f / zoom) {
                    currentCenters.push_back(prevPos);
                    currentRadii.push_back(epi.amp);
                    armSegments.push_back(prevPos); armSegments.push_back(newPos);
                }
            }
        }

        // --- Render Frame ---
        fbo.Bind();
        glDisable(GL_SCISSOR_TEST);
        glClearColor(bgColor.r, bgColor.g, bgColor.b, bgColor.a);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        float aspect = (float)RENDER_W / RENDER_H;
        float hView = 1000.0f / zoom;
        float wView = hView * aspect;
        glm::mat4 proj = glm::ortho(-wView/2, wView/2, -hView/2, hView/2);
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(pan, 0.0f));

        if (showRef && !pathPoints.empty()) {
            glLineWidth(1.0f);
            lineShader.Use(); lineShader.SetMat4("uProjection", proj); lineShader.SetMat4("uView", view);
            pathRenderer.UpdateAndDraw(pathPoints, lineShader, glm::vec4(0.2, 0.2, 0.2, 0.5));
        }
        if (showTrail && !trail.empty()) {
            glLineWidth(strokeWidth);
            lineShader.Use(); lineShader.SetMat4("uProjection", proj); lineShader.SetMat4("uView", view);
            trailRenderer.UpdateAndDraw(trail, lineShader, inkColor);
        }
        if (showCircles && !currentCenters.empty()) {
            circleShader.Use(); circleShader.SetMat4("uProjection", proj); circleShader.SetMat4("uView", view);
            circleShader.SetVec4("uColor", 1.0, 1.0, 1.0, 0.2);
            circleBatch.Draw(currentCenters, currentRadii, circleShader);
        }
        if (showArms && !armSegments.empty()) {
            glLineWidth(1.0f);
            lineShader.Use();
            armBatch.Draw(armSegments, lineShader, glm::vec4(1.0, 1.0, 1.0, 0.5));
        }

        if (recording && exporter) {
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            exporter->CaptureFrame();
        }

        int scrW, scrH; SDL_GetWindowSize(window, &scrW, &scrH);
        fbo.Unbind(scrW, scrH);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        screenQuad.Draw(fbo.tex);

        // --- UI ---
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Loading Modal
        if (isLoading) {
            if (!ImGui::IsPopupOpen("Loading")) ImGui::OpenPopup("Loading");
        }
        bool open = true;
        if (ImGui::BeginPopupModal("Loading", &open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::Text("%s", statusMessage.c_str());
            if (!isLoading) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Main Control Panel
        ImGui::Begin("Fourier Forge", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        
        // Header / Load
        if (ImGui::Button(" Load SVG ")) {
            IGFD::FileDialogConfig config; config.path = "."; config.countSelectionMax = 1; config.flags = ImGuiFileDialogFlags_Modal;
            IGFD::FileDialog::Instance()->OpenDialog("ChooseFile", "Select SVG", ".svg", config);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", statusMessage.c_str());
        
        ImGui::Separator();

        // PLAYBACK CONTROLS
        ImGui::Text("Playback");
        if (ImGui::Button(paused ? "  PLAY  " : " PAUSE ")) paused = !paused;
        ImGui::SameLine();
        
        // UPDATED RESET LOGIC
        if (ImGui::Button(" RESET ")) { 
            time = 0.0f; 
            trail.clear(); 
            paused = true;
            
            // "Show ghost outline, nothing others"
            showRef = true;
            showCircles = false;
            showArms = false;
            showTrail = false;
        }
        
        // Progress Bar
        ImGui::SameLine();
        ImGui::PushItemWidth(200);
        ImGui::SliderFloat("##Progress", &time, 0.0f, 1.0f, "%.2f");
        ImGui::PopItemWidth();
        
        ImGui::SliderFloat("Speed", &speed, 0.0f, 2.0f, "%.3f", ImGuiSliderFlags_Logarithmic);

        // TABS FOR SETTINGS
        if (ImGui::BeginTabBar("SettingsTabs")) {
            
            // TAB: EXPORT
            if (ImGui::BeginTabItem("Export")) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f)); // Spacing
                ImGui::TextColored(ImVec4(1, 0.8f, 0, 1), "Cinematic Auto-Render");
                ImGui::TextWrapped("Automatically zooms in, tracks the pen, and saves the video when the drawing completes.");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                
                if (ImGui::Button("START CINEMATIC SHOT", ImVec2(-1, 40))) {
                    if (!epicycles.empty()) {
                        cinematicMode = true;
                        recording = true;
                        exporter = std::make_unique<VideoExporter>(RENDER_W, RENDER_H, 60);
                        time = 0.0f;
                        trail.clear();
                        paused = false;
                        autoFollow = true;
                        trailLength = 0; 
                        
                        // FORCE ENABLE VISUALS FOR RECORDING
                        showRef = false;
                        showCircles = true;
                        showArms = true;
                        showTrail = true;
                    }
                }
                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Manual Recording");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                if (ImGui::Button(recording && !cinematicMode ? "STOP RECORDING" : "START MANUAL REC", ImVec2(-1, 30))) {
                    recording = !recording;
                    if (recording) {
                        exporter = std::make_unique<VideoExporter>(RENDER_W, RENDER_H, 60);
                        time = 0.0f; trail.clear();
                        
                        // Manual record usually implies we want to see the drawing
                        showTrail = true;
                    } else exporter.reset();
                }
                if (recording) {
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "RECORDING IN PROGRESS...");
                }
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::EndTabItem();
            }

            // TAB: VISUALS
            if (ImGui::BeginTabItem("Visuals")) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f)); // Spacing
                ImGui::Text("Visibility");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                ImGui::Checkbox("Circles", &showCircles);
                ImGui::SameLine(); ImGui::Checkbox("Arms", &showArms);
                ImGui::SameLine(); ImGui::Checkbox("Trail", &showTrail);
                ImGui::Checkbox("Ghost Reference", &showRef);
                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                ImGui::Spacing();
                ImGui::Text("Colors & Style");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                ImGui::ColorEdit3("Ink Color", (float*)&inkColor);
                ImGui::Checkbox("Rainbow Ink", &rainbowMode);
                ImGui::SliderFloat("Stroke Width", &strokeWidth, 1.0f, 10.0f);
                ImGui::ColorEdit3("Background", (float*)&bgColor);
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
                
                ImGui::Spacing();
                ImGui::Text("Trail Mode");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                if(ImGui::RadioButton("Infinite", trailLength == 0)) trailLength = 0;
                ImGui::SameLine();
                if(ImGui::RadioButton("Snake", trailLength > 0)) trailLength = 1000;
                if(trailLength > 0) ImGui::SliderInt("Tail Len", &trailLength, 100, 5000);
                ImGui::Dummy(ImVec2(0.0f, 5.0f));

                ImGui::EndTabItem();
            }

            // TAB: CAMERA & MATH
            if (ImGui::BeginTabItem("Camera/Math")) {
                ImGui::Dummy(ImVec2(0.0f, 8.0f)); // Spacing
                ImGui::Text("Camera");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                ImGui::SliderFloat("Zoom", &zoom, 0.1f, 50.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
                ImGui::Checkbox("Auto-Follow Pen", &autoFollow);
                if(ImGui::Button("Reset View")) { zoom=1.0f; pan=glm::vec2(0,0); autoFollow=false; }
                ImGui::Dummy(ImVec2(0.0f, 8.0f));

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                ImGui::Text("Approximation");
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                ImGui::Dummy(ImVec2(0.0f, 3.0f));
                int maxE = (int)epicycles.size();
                ImGui::SliderInt("Vectors", &activeCircles, 1, maxE);
                ImGui::TextDisabled("Using %d of %d available vectors", activeCircles, maxE);
                ImGui::Dummy(ImVec2(0.0f, 5.0f));
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        // File Dialog Logic
        ImVec2 minSize(800, 600);
        ImVec2 maxSize(FLT_MAX, FLT_MAX);
        if (IGFD::FileDialog::Instance()->Display("ChooseFile", ImGuiWindowFlags_NoCollapse, minSize, maxSize)) {
            if (IGFD::FileDialog::Instance()->IsOk()) {
                std::string path = IGFD::FileDialog::Instance()->GetFilePathName();
                AsyncLoad(path);
            }
            IGFD::FileDialog::Instance()->Close();
        }

        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    if(isLoading && loadingFuture.valid()) loadingFuture.wait();
    exporter.reset();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}