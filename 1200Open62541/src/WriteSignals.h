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

static const std::vector<Signal> writeSignals = {
    { "Test1", "Bool", 0, 0 },
    { "Test2", "DInt", 2, 0 },
    { "Test3", "DInt", 6, 0 },
    { "Test4", "Real", 10, 0 },
    { "Test5", "Bool", 14, 0 },
    { "Test6", "DInt", 16, 0 },
};
