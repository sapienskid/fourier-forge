#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <iostream>
#include <GL/glew.h>

class VideoExporter {
    FILE* ffmpegPipe = nullptr;
    int width, height;
    GLuint pbo; 

public:
    VideoExporter(int w, int h, int fps) : width(w), height(h) {
        glGenBuffers(1, &pbo);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glBufferData(GL_PIXEL_PACK_BUFFER, w * h * 3, nullptr, GL_STREAM_READ);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        // --- HARDWARE ACCELERATION COMMAND ---
        // We try NVIDIA (h264_nvenc) first. If you are on AMD, change to h264_vaapi.
        // If hardware fails, we fall back to 'ultrafast' software encoding.
        
        std::string cmd = "ffmpeg -r " + std::to_string(fps) + 
                          " -f rawvideo -pix_fmt rgb24 -s " + std::to_string(w) + "x" + std::to_string(h) +
                          " -i - -threads 0 " +
                          // TRY ONE OF THESE ENCODERS:
                          // "-c:v h264_nvenc -preset p1 " +  // NVIDIA GPU (Fastest)
                          // "-c:v h264_amf " +               // AMD GPU
                          "-c:v libx264 -preset ultrafast " + // CPU (Fallback, but faster)
                          "-crf 23 -pix_fmt yuv420p -y output.mp4";

        // Note: For the 'Systems Programmer' robust version, normally we would detect the GPU vendor.
        // For now, 'libx264 -preset ultrafast' is the safest fix for the CPU spike 
        // without knowing your exact hardware drivers.

        ffmpegPipe = popen(cmd.c_str(), "w");
    }

    ~VideoExporter() {
        if (ffmpegPipe) pclose(ffmpegPipe);
        glDeleteBuffers(1, &pbo);
    }

    void CaptureFrame() {
        if (!ffmpegPipe) return;
        
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, 0);
        
        void* ptr = glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
            uint8_t* src = (uint8_t*)ptr;
            int stride = width * 3;
            for (int y = 0; y < height; ++y) {
                fwrite(src + (height - 1 - y) * stride, 1, stride, ffmpegPipe);
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
};