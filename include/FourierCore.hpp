#pragma once
#include <vector>
#include <complex>
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

struct Epicycle {
    std::complex<double> value;
    int frequency;
    float amp;
    float phase;

    std::complex<double> evaluate(double t) const {
        // angle = freq * 2pi * t
        const double angle = frequency * (2.0 * M_PI * t);
        return value * std::exp(std::complex<double>(0, angle));
    }
};

class FourierTransform {
public:
    static std::vector<Epicycle> ComputeDFT(const std::vector<glm::vec2>& path) {
        size_t N = path.size();
        std::vector<Epicycle> fourier(N);

        // Basic O(N^2) DFT
        // For N=3000, operations ~= 9,000,000 (Fast enough for background thread)
        for (int k = 0; k < (int)N; ++k) {
            std::complex<double> sum(0, 0);
            
            // Remap frequency k: 0, 1... N/2, -N/2 ... -1
            int freq = k;
            if (k > (int)N / 2) freq -= N;

            for (int n = 0; n < (int)N; ++n) {
                double phi = (2.0 * M_PI * k * n) / N;
                std::complex<double> c(path[n].x, path[n].y);
                // e^(-i * phi) = cos(phi) - i*sin(phi)
                sum += c * std::complex<double>(std::cos(phi), -std::sin(phi));
            }

            sum /= (double)N;

            fourier[k] = {
                sum,
                freq,
                (float)std::abs(sum),
                (float)std::arg(sum)
            };
        }

        // Sort by amplitude (Largest circles first)
        std::sort(fourier.begin(), fourier.end(), [](const Epicycle& a, const Epicycle& b) {
            return a.amp > b.amp;
        });

        return fourier;
    }
};