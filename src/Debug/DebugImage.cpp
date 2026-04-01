#include "Debug/DebugImage.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <cmath>
#include <algorithm>
#include <vector>
#include <cstring>
#include <cstdint>

// ── BMP file writing ─────────────────────────────────────────────────────────

static bool saveBMP(const std::string& path, int w, int h, const uint8_t* pixels) {
    // pixels: w*h*3 bytes, bottom-up, BGR order
    int rowBytes = ((w * 3 + 3) / 4) * 4; // Row must be 4-byte aligned
    int dataSize = rowBytes * h;

    BITMAPFILEHEADER bfh = {};
    bfh.bfType = 0x4D42; // "BM"
    bfh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

    BITMAPINFOHEADER bih = {};
    bih.biSize = sizeof(BITMAPINFOHEADER);
    bih.biWidth = w;
    bih.biHeight = h; // positive = bottom-up
    bih.biPlanes = 1;
    bih.biBitCount = 24;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = dataSize;

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write((char*)&bfh, sizeof(bfh));
    f.write((char*)&bih, sizeof(bih));

    // Write row-by-row with padding
    for (int y = 0; y < h; y++) {
        f.write((const char*)(pixels + y * w * 3), w * 3);
        // Pad to 4-byte boundary
        int pad = rowBytes - w * 3;
        if (pad > 0) {
            char zeros[4] = {};
            f.write(zeros, pad);
        }
    }

    return f.good();
}

// ── Bresenham line drawing ───────────────────────────────────────────────────

static void drawLine(uint8_t* pixels, int imgW, int imgH,
                     int x0, int y0, int x1, int y1,
                     int lineW, uint8_t r, uint8_t g, uint8_t b)
{
    // Simple thick line: draw the base line and neighbors within lineW/2
    int hw = lineW / 2;

    int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        // Plot thick point
        for (int oy = -hw; oy <= hw; oy++) {
            for (int ox = -hw; ox <= hw; ox++) {
                int px = x0 + ox, py = y0 + oy;
                if (px >= 0 && px < imgW && py >= 0 && py < imgH) {
                    int idx = (py * imgW + px) * 3;
                    pixels[idx + 0] = b;
                    pixels[idx + 1] = g;
                    pixels[idx + 2] = r;
                }
            }
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void drawCircle(uint8_t* pixels, int imgW, int imgH,
                       int cx, int cy, int radius,
                       uint8_t r, uint8_t g, uint8_t b)
{
    for (int y = cy - radius; y <= cy + radius; y++) {
        for (int x = cx - radius; x <= cx + radius; x++) {
            if (x >= 0 && x < imgW && y >= 0 && y < imgH) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius) {
                    int idx = (y * imgW + x) * 3;
                    pixels[idx + 0] = b;
                    pixels[idx + 1] = g;
                    pixels[idx + 2] = r;
                }
            }
        }
    }
}

// ── Debug image generation ───────────────────────────────────────────────────

void generateDebugBMP(
    const std::string& gcodePath,
    const std::string& outputPath,
    const Config& config,
    const std::vector<DrillHole>& holes)
{
    double beamSize = config.machine.laser_beam_diameter;
    std::regex coordRe(R"(X([-\d.]+)\s*Y([-\d.]+))");

    // Pass 1: find coordinate bounds
    double minX = 1e18, minY = 1e18, maxX = -1e18, maxY = -1e18;

    {
        std::ifstream f(gcodePath);
        if (!f.is_open()) return;
        std::string line;
        while (std::getline(f, line)) {
            std::smatch m;
            if (std::regex_search(line, m, coordRe)) {
                double x = std::stod(m[1].str());
                double y = std::stod(m[2].str());
                minX = std::min(minX, x); minY = std::min(minY, y);
                maxX = std::max(maxX, x); maxY = std::max(maxY, y);
            }
        }
    }

    if (minX > 1e17) return; // No coordinates found

    double margin = 1.0;
    minX -= margin; minY -= margin;
    maxX += margin; maxY += margin;

    double wMM = maxX - minX, hMM = maxY - minY;
    int outW = (int)std::ceil(wMM / beamSize);
    int outH = (int)std::ceil(hMM / beamSize);

    // Cap output size
    if ((long long)outW * outH > 25000000LL) {
        double s = std::sqrt(25000000.0 / ((double)outW * outH));
        outW = (int)(outW * s);
        outH = (int)(outH * s);
    }
    if (outW < 10 || outH < 10) return;

    double effRes = wMM / outW;
    int lineWidth = std::max(1, (int)std::round(beamSize / effRes));

    // Allocate pixel buffer (bottom-up, BGR)
    std::vector<uint8_t> pixels(outW * outH * 3, 255); // white background

    auto toPx = [&](double x, double y, int& px, int& py) {
        px = (int)((x - minX) / effRes);
        py = (outH - 1) - (int)((y - minY) / effRes); // flip Y for BMP bottom-up
    };

    // Pass 2: draw laser paths
    {
        std::ifstream f(gcodePath);
        if (!f.is_open()) return;

        double curX = 0, curY = 0;
        bool laserOn = false;
        bool inDrill = false;

        std::string line;
        while (std::getline(f, line)) {
            // Trim
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();

            if (line == "USE_LASER")   { inDrill = false; continue; }
            if (line == "USE_SPINDLE") { inDrill = true; laserOn = false; continue; }
            if (line.find("LASER_SET") == 0) { laserOn = true; continue; }
            if (line == "LASER_OFF")   { laserOn = false; continue; }

            std::smatch m;
            if (std::regex_search(line, m, coordRe)) {
                double nx = std::stod(m[1].str());
                double ny = std::stod(m[2].str());

                if (laserOn && !inDrill) {
                    int x0, y0, x1, y1;
                    toPx(curX, curY, x0, y0);
                    toPx(nx, ny, x1, y1);
                    drawLine(pixels.data(), outW, outH, x0, y0, x1, y1,
                             lineWidth, 0, 0, 0); // Black
                }

                curX = nx; curY = ny;
            }
        }
    }

    // Draw drill holes
    if (!holes.empty()) {
        double xOff = config.machine.x_offset;
        double yOff = config.machine.y_offset;
        double toolD = config.machine.spindle_tool_diameter;

        for (auto& h : holes) {
            int cx, cy;
            toPx(h.x + xOff, h.y + yOff, cx, cy);

            // Green: required hole diameter
            int rReq = std::max(3, (int)((h.diameter / 2.0) / effRes));
            drawCircle(pixels.data(), outW, outH, cx, cy, rReq, 0, 180, 0);

            // Blue: tool diameter (overlaid)
            int rTool = std::max(2, (int)((toolD / 2.0) / effRes));
            drawCircle(pixels.data(), outW, outH, cx, cy, rTool, 0, 0, 255);
        }
    }

    saveBMP(outputPath, outW, outH, pixels.data());
}
