#pragma once

#include "module/private/module_interface.h"

namespace meha::internal
{

class LogModule : public Module
{
public:
    explicit LogModule();
    uint32_t priority() const override;
    InitTime initTime() const override;

protected:
    bool initInternel() override;
};

class HookModule : public Module
{
public:
    explicit HookModule();
    void cleanup() override;
    uint32_t priority() const override;
    InitTime initTime() const override;

protected:
    bool initInternel() override;
};

}