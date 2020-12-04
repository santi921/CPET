#ifndef UTILITIES_H
#define UTILITIES_H

/* C++ STL HEADER FILES */
#include <vector>
#include <string>
#include <functional>
#include <random>
#include <fstream>

/* EXTERNAL LIBRARY HEADER FILES */
#include "spdlog/spdlog.h"

/* CPET HEADER FILES */
#include "Exceptions.h"

[[nodiscard]] inline std::unique_ptr<std::mt19937> &randomNumberGenerator() noexcept {
    static thread_local std::unique_ptr<std::mt19937> generator = nullptr;
    if (generator == nullptr){
        generator = std::make_unique<std::mt19937>(std::random_device()());
    }
    return generator;
}

void forEachLineIn(const std::string& file, const std::function<void(const std::string&)>& func);

std::vector<std::string> split(std::string_view str, char delim);

template<class T>
void filter(std::vector<T>& list, const T& remove= T()) noexcept(true) {
    for (typename std::vector<T>::size_type i = 0; i < list.size(); i++){
        if(list[i] == remove){
            list.erase(list.begin() + static_cast<long>(i));
            i--;
        }
    }
}

template<class InputIt, class UnaryPredicate>
InputIt find_if_ex(InputIt first, InputIt last, UnaryPredicate p){
    InputIt loc = std::find_if(first, last, p);
    if (loc == last){
        throw cpet::value_not_found("Could not find element in containiner");
    }
    return loc;
}

template<class T>
void writeToFile(const std::string& file, const std::vector<T>& out){
    std::ofstream outFile(file, std::ios::out);
    if(outFile.is_open()){
        for(const auto& line : out){
            outFile << line << '\n';
        }
        outFile << std::flush;
    }
    else{
        throw cpet::io_error("Could not open file " + file);
    }
}

#endif //UTILITIES_H
