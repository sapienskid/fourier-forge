#pragma once
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <vector>
#include <algorithm>
#include <string>
#include <iostream>

// --- Shader Helper ---
struct Shader {
    GLuint id;

    // Constructor with error checking
    Shader(const char* vSrc, const char* fSrc) {
        auto compile = [](GLenum type, const char* src) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            
            GLint success;
            glGetShaderiv(s, GL_COMPILE_STATUS, &success);
            if (!success) {
                char infoLog[512];
                glGetShaderInfoLog(s, 512, NULL, infoLog);
                std::cerr << "Shader Compile Error:\n" << infoLog << std::endl;
            }
            return s;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, vSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fSrc);
        id = glCreateProgram();
        glAttachShader(id, vs);
        glAttachShader(id, fs);
        glLinkProgram(id);

        GLint success;
        glGetProgramiv(id, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(id, 512, NULL, infoLog);
            std::cerr << "Shader Link Error:\n" << infoLog << std::endl;
        }

        glDeleteShader(vs);
        glDeleteShader(fs);
    }

    ~Shader() { glDeleteProgram(id); }

    void Use() const { glUseProgram(id); }

    void SetMat4(const char* name, const glm::mat4& m) const {
        glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, &m[0][0]);
    }

    void SetVec4(const char* name, float r, float g, float b, float a) const {
        glUniform4f(glGetUniformLocation(id, name), r, g, b, a);
    }
};

// --- Instanced Circle Renderer ---
class CircleBatch {
    GLuint vao, vbo, instanceVBO;
    size_t maxInstances;

    struct InstanceData {
        glm::vec2 center;
        float radius;
        float padding; 
    };

public:
    CircleBatch(size_t count) : maxInstances(count) {
        float quadVertices[] = {
            -1.0f, -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &instanceVBO);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, maxInstances * sizeof(InstanceData), nullptr, GL_STREAM_DRAW);
        
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)0);
        glVertexAttribDivisor(1, 1);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(sizeof(glm::vec2)));
        glVertexAttribDivisor(2, 1);

        glBindVertexArray(0);
    }

    ~CircleBatch() {
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &instanceVBO);
    }

    void Draw(const std::vector<glm::vec2>& centers, const std::vector<float>& radii, Shader& shader) {
        size_t count = std::min(centers.size(), maxInstances);
        if (count == 0) return;

        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        static std::vector<InstanceData> scratch;
        if (scratch.size() < count) scratch.resize(count);
        
        for(size_t i=0; i<count; ++i) {
            scratch[i] = { centers[i], radii[i], 0.0f };
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(InstanceData), scratch.data());

        shader.Use();
        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)count);
        glBindVertexArray(0);
    }
};

// --- Dynamic Line Batch ---
class LineBatch {
    GLuint vao, vbo;
    size_t maxLines;
public:
    LineBatch(size_t maxL) : maxLines(maxL) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, maxLines * 2 * sizeof(glm::vec2), nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glBindVertexArray(0);
    }
    ~LineBatch() { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); }

    void Draw(const std::vector<glm::vec2>& endpoints, Shader& shader, glm::vec4 color) {
        size_t count = std::min(endpoints.size(), maxLines * 2);
        if (count < 2) return;

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(glm::vec2), endpoints.data());

        shader.Use();
        shader.SetVec4("uColor", color.r, color.g, color.b, color.a);
        shader.SetMat4("uModel", glm::mat4(1.0f));
        
        glBindVertexArray(vao);
        glDrawArrays(GL_LINES, 0, (GLsizei)count);
        glBindVertexArray(0);
    }
};

// --- Trail Renderer ---
class TrailRenderer {
    GLuint vao, vbo;
    size_t maxPoints;
public:
    TrailRenderer(size_t maxP) : maxPoints(maxP) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, maxPoints * sizeof(glm::vec2), nullptr, GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), (void*)0);
        glBindVertexArray(0);
    }
    
    ~TrailRenderer() { glDeleteVertexArrays(1, &vao); glDeleteBuffers(1, &vbo); }

    void UpdateAndDraw(const std::vector<glm::vec2>& points, Shader& shader, glm::vec4 color) {
        if(points.empty()) return;
        size_t count = std::min(points.size(), maxPoints);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(glm::vec2), points.data());

        shader.Use();
        shader.SetVec4("uColor", color.r, color.g, color.b, color.a);
        shader.SetMat4("uModel", glm::mat4(1.0f));

        glBindVertexArray(vao);
        glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)count);
        glBindVertexArray(0);
    }
};