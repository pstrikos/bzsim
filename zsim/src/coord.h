#ifndef COORD_H_
#define COORD_H_
#include "unordered_map"
#include <string>
#include <stdint.h>
#include <sys/time.h>

template<typename T> struct coordinates{T x; T y;};
template<typename T> struct doubleCoordinates{
    coordinates<T> src;
    coordinates<T> dest;
};

template<typename T>
std::ostream& operator<<(std::ostream& os, const coordinates<T>& c) {
    os << "(" << c.x << ", " << c.y << ")";
    return os;
}

template<typename T>
bool operator==(const coordinates<T>& a, const coordinates<T>& b) {
    return a.x == b.x && a.y == b.y;
}

extern std::unordered_map<std::string, std::vector<coordinates<int>>> networkCoordMap;

#endif