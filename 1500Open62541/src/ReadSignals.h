#pragma once
#include <string>
#include <vector>

#ifndef SIGNAL_STRUCT_DEFINED
#define SIGNAL_STRUCT_DEFINED
struct Signal
{
    std::string name;
    std::string dataType;
    int byteOffset;
    int bitOffset;
};
#endif

static const std::vector<Signal> readSignals = {
    { "Test7", "Bool", 0, 0 },
    { "Test8", "DInt", 2, 0 },
    { "Test9", "DInt", 6, 0 },
    { "Test10", "Real", 10, 0 },
    { "Test11", "Bool", 14, 0 },
    { "Test12", "DInt", 16, 0 },
};
