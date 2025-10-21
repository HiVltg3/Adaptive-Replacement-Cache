#include "printResults.h"
void printResults(const std::string& name, int capacity, int get_ops, int hits) {
    std::cout << name << " | cap=" << capacity
        << " | gets=" << get_ops
        << " | hits=" << hits
        << " | hit_rate=" << (get_ops ? (hits * 100.0 / get_ops) : 0) << "%\n";
}