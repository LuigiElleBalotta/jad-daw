#pragma once
#include <complex>
#include <vector>

namespace lpc {

// A radix-2 complex FFT of a fixed power-of-two size (offline analysis and the flex processors; it allocates only in the constructor).
class Fft {
public:
    explicit Fft(int size);  // size must be a power of two, at least 2
    int size() const { return n_; }
    // In place. The inverse is scaled by 1/size, so inverse(forward(x)) == x.
    void forward(std::complex<double>* data) const { run(data, false); }
    void inverse(std::complex<double>* data) const { run(data, true); }

    static int nextPowerOfTwo(int n);

private:
    void run(std::complex<double>* data, bool inverse) const;
    int n_ = 0;
    std::vector<int> rev_;
    std::vector<std::complex<double>> twiddle_;  // exp(-2 pi i k / n), k < n/2
};

}  // namespace lpc
