#include "MyInput.h"

bool dataInput(int& data) {
    if (!(std::cin >> data)) {
        // 清除错误状态和缓冲区
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "输入无效。" << std::endl;

        return false;
    }
    std::cin.get();
    return true;
}

bool dataInput(std::string& data) {
    if (!(std::cin >> data)) {
        // 清除错误状态和缓冲区
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "输入无效。" << std::endl;

        return false;
    }
    std::cin.get();
    return true;
}