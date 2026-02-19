#ifndef MODEL_H
#define MODEL_H

struct coord_pair {
    double x = 0.0;
    double y = 0.0;
};

struct coord_ref {
    int start = 0;
    int end = 0;
    double x = 0.0;
    double y = 0.0;
};

struct circle_pair {
    double cx = 0.0;
    double cy = 0.0;
    double r = 0.0;
};

struct circle_ref {
    int radius_start = 0;
    int radius_end = 0;
    double cx = 0.0;
    double cy = 0.0;
    double r = 0.0;
};

struct rectangle_pair {
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
};

struct rectangle_ref {
    int x2_start = 0;
    int x2_end = 0;
    int y2_start = 0;
    int y2_end = 0;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
};

struct ellipse_pair {
    double cx = 0.0;
    double cy = 0.0;
    double rx = 0.0;
    double ry = 0.0;
};

struct ellipse_ref {
    int rx_start = 0;
    int rx_end = 0;
    int ry_start = 0;
    int ry_end = 0;
    double cx = 0.0;
    double cy = 0.0;
    double rx = 0.0;
    double ry = 0.0;
};

struct bezier_pair {
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double x3 = 0.0;
    double y3 = 0.0;
};

struct bezier_ref {
    int x1_start = 0;
    int x1_end = 0;
    int y1_start = 0;
    int y1_end = 0;
    int x2_start = 0;
    int x2_end = 0;
    int y2_start = 0;
    int y2_end = 0;
    double x0 = 0.0;
    double y0 = 0.0;
    double x1 = 0.0;
    double y1 = 0.0;
    double x2 = 0.0;
    double y2 = 0.0;
    double x3 = 0.0;
    double y3 = 0.0;
};

#endif
