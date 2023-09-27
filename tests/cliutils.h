#pragma once
#include <string>
#include <map>

struct CLIParam {
    std::string name;
    std::string description;
    bool optional = false;
    std::string default_v;
};

struct CLIParams {
    std::vector<CLIParam> params;
    int mOptionalParams = 0;
    int mMandatoryParams = 0;
    std::map<std::string, int> mParamToIndex;
public:

    CLIParams(std::vector<CLIParam> params_) : params(std::move(params_)) {
        bool optional = false;
        int idx = 0;
        for (auto& p : params) {
            mParamToIndex[p.name] = idx;
            idx++;
            if (p.optional) {
                optional = true;
                mOptionalParams++;
            }
            else {
                mMandatoryParams++;
            }
            if (optional && !p.optional) {
                throw std::runtime_error("Optional wrong");
            }
        }
    }
    std::string getShortString() const {
        std::stringstream ss;
        for (auto& p : params) {
            if (p.optional) {
                ss << "<";
            }
            ss << p.name;
            if (p.optional) {
                ss << ">";
            }
            ss << " ";
        }
        return ss.str();
    }
    std::string getFullString() const {
        std::stringstream ss;
        for (auto& p : params) {
            ss << "\t";
            ss << p.name;
            ss << "\t\t";
            ss << p.description;
            ss << std::endl;
        }
        return ss.str();
    }
    bool checkCountArgc(int argc) {
        argc--;
        if (argc < mMandatoryParams) {
            return false;
        }
        if (argc > mMandatoryParams + mOptionalParams) {
            return false;
        }
        return true;
    }
    const char* getParamValue(const char* name, char* argv[], int argc) {
        auto it = mParamToIndex.find(name);
        if (it == mParamToIndex.end()) {
            return nullptr;
        }
        argc--;
        if (it->second >= argc) {
            return params[it->second].default_v.c_str();
        }
        return argv[it->second + 1];
    }
};

template<class T>
bool vector_contains(const std::vector<T>& vec, const T& val) {
    for (auto& e : vec) {
        if (e == val)
            return true;
    }
    return false;
}
