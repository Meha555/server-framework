#include "cmd.h"
#include "log.h"
#include <iostream>
#include <sstream>
#include <string>

namespace meha::utils
{

static auto g_logger = GET_ROOT_LOGGER();

bool ArgParser::addFlag(const Flag &flag)
{
    return m_flagsPattern.emplace(flag.longKey(), Data{flag, false}).second;
}

bool ArgParser::addOption(const Option &option)
{
    return m_optionsPattern.emplace(option.longKey(), Data{option, false}).second;
}
// TODO 这里是需要实现Arg的移动构造函数吗(各成员可移动，这个类会是可移动的吗)

bool ArgParser::addArg(const Arg &arg)
{
    switch (arg.type()) {
    case Arg::Type::Flag:
        return addFlag(static_cast<const Flag &>(arg));
    case Arg::Type::Option:
        return addOption(static_cast<const Option &>(arg));
    }
}

bool ArgParser::parseArgs(int argc, char *argv[])
{
    std::stringstream ss;
    for (int i = 1; i < argc; ++i) {
        ss << argv[i] << " ";
    }
    m_isParsed = doParse(ss);
    if (!m_isParsed) {
        LOG_FATAL(g_logger, "Failed to parse args");
        reset();
    }
    return m_isParsed;
}

bool ArgParser::parseArgs()
{
    std::string input;
    std::getline(std::cin, input);

    std::stringstream ss(input);
    m_isParsed = doParse(ss);
    if (!m_isParsed) {
        LOG_FATAL(g_logger, "Failed to parse args");
        reset();
    }
    return m_isParsed;
}

bool ArgParser::isFlagSet(const std::string &key) const
{
    return std::any_of(m_flagsPattern.cbegin(), m_flagsPattern.cend(), [&key](const auto &arg) {
        return key == arg.second.data.longKey() || key == arg.second.data.shortKey();
    });
}

std::optional<std::any> ArgParser::getOptionValue(const std::string& key) const
{
    auto it = std::find_if(m_optionsPattern.cbegin(), m_optionsPattern.cend(), [&key](const auto &arg) {
        return key == arg.second.data.longKey() || key == arg.second.data.shortKey();
    });
    return it == m_optionsPattern.cend() ? std::nullopt : it->second.data.value();
}

std::string ArgParser::dumpAll() const
{
    std::stringstream ss;
    for (const auto& arg : m_flagsPattern) {
        ss << arg.second.data << '\n';
    }
    for (const auto& arg : m_optionsPattern) {
        ss << arg.second.data << '\n'; //" is " << (arg.isSet ? "set" : "not set") <<
    }
    return ss.str();
}

void ArgParser::reset()
{
    m_isParsed = false;
    m_flags.clear();
    m_options.clear();
    for (auto &arg : m_flagsPattern) {
        arg.second.isValid = false;
    }
    for (auto &arg : m_optionsPattern) {
        arg.second.isValid = false;
    }
}

bool ArgParser::doParse(std::stringstream &ss)
{
    // 1. 扫描输入的结果，准备用于解析的数据
    // 旧写法，当时计划着--option value这种格式的，但是后来感觉这样没必要，反而不如--option=value这样好用
    // enum State {
    //     DO_SCAN,
    //     IS_OPTION,
    // };
    // State state = DO_SCAN;
    // std::string token, preToken;
    // while (ss >> token) {
    //     preToken = token;
    //     switch (state) {
    //     case DO_SCAN:
    //         if (token[0] == '-') {
    //             state = IS_OPTION;
    //         } else {
    //             m_flags.emplace_back(token);
    //         }
    //         break;
    //     case IS_OPTION:
    //         m_options[preToken] = token;
    //         break;
    //     }
    // }
    // // 最后一个是选项且选项缺少数值
    // if (m_options.count(preToken) == 0) {
    //     m_options[preToken] = "";
    // }
    std::string token;
    while (ss >> token) {
        size_t idx = token.find('=');
        if (idx != std::string::npos) {
            std::string key = token.substr(0, idx);
            std::string value = token.substr(idx + 1);
            m_options[key] = value;
        } else {
            m_flags.emplace_back(token);
        }
    }
    // 2. 开始解析
    return doParseFlags() && doParseOptions();
}

bool ArgParser::doParseFlags()
{
    for (auto &flag : m_flags) {
        for (auto &arg : m_flagsPattern) {
            if (flag == arg.second.data.longKey() || flag == arg.second.data.shortKey()) {
                arg.second.isValid = true;
                break;
            }
        }
    }
    bool isSet = true;
    for (auto &arg : m_flagsPattern) {
        if (!arg.second.isValid && arg.second.data.isRequired()) {
            LOG(g_logger, FATAL) << arg.second.data << " is required but not set";
            isSet = false;
        }
    }
    return isSet;
}

bool ArgParser::doParseOptions()
{
    bool checkRules = true;
    for (auto &opt : m_options) {
        for (auto &arg : m_optionsPattern) {
            if (opt.first == arg.second.data.longKey() || opt.first == arg.second.data.shortKey()) {
                arg.second.isValid = arg.second.data.isFitRules();
                if (!arg.second.isValid) {
                    checkRules = false;
                    LOG(g_logger, FATAL) << arg.second.data << " do not fit the rules";
                    continue;
                }
                arg.second.data.setValue(opt.second); // 设置value不会改变键值，也就不会改变迭代顺序
            }
        }
    }
    return checkRules;
}

}