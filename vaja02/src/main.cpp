#include <opencv2/Accelerate.fr>
#include <iostream>
#include <chrono>

int main() {
    // Print version so you can verify the build being used
    std::cout << "Hello OpenCV " << CV_VERSION << std::endl;

    const int W = 640, H = 400;
    cv::Mat frame(H, W, CV_8UC3);

    // Ball state
    cv::Point2f pos(W * 0.2f, H * 0.3f);
    cv::Point2f vel(3.7f, 2.4f); // pixels per frame
    const int radius = 20;

    // Colors (BGR)
    const cv::Scalar bg(30, 30, 30);
    const cv::Scalar ballColor(0, 255, 0);
    const cv::Scalar axesColor(70, 70, 70);
    const cv::Scalar textColor(255, 255, 255);

    cv::namedWindow("OpenCV GUI Test", cv::WINDOW_AUTOSIZE);

    auto t0 = std::chrono::high_resolution_clock::now();
    int frames = 0;
    double fps = 0.0;

    while (true) {
        // Background
        frame.setTo(bg);

        // Draw simple axes/grid
        cv::line(frame, {0, H/2}, {W, H/2}, axesColor, 1, cv::LINE_AA);
        cv::line(frame, {W/2, 0}, {W/2, H}, axesColor, 1, cv::LINE_AA);

        // Update ball
        pos += vel;
        if (pos.x < radius || pos.x > W - radius) vel.x = -vel.x;
        if (pos.y < radius || pos.y > H - radius) vel.y = -vel.y;
        pos.x = std::clamp(pos.x, (float)radius, (float)(W - radius));
        pos.y = std::clamp(pos.y, (float)radius, (float)(H - radius));

        // Draw ball
        cv::circle(frame, pos, radius, ballColor, -1, cv::LINE_AA);

        // FPS counter
        frames++;
        auto t1 = std::chrono::high_resolution_clock::now();
        double secs = std::chrono::duration<double>(t1 - t0).count();
        if (secs >= 0.5) { // update every ~0.5s
            fps = frames / secs;
            frames = 0;
            t0 = t1;
        }
        std::string label = "OpenCV " + std::string(CV_VERSION) + "  |  FPS: " + cv::format("%.1f", fps);
        cv::putText(frame, label, {10, 24}, cv::FONT_HERSHEY_SIMPLEX, 0.7, textColor, 2, cv::LINE_AA);

        // Show
        cv::imshow("OpenCV GUI Test", frame);

        // Quit on ESC or 'q'
        int key = cv::waitKey(16);
        if (key == 27 || key == 'q' || key == 'Q') break;
    }

    return 0;
}
