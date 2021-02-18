/*
// Copyright (c) 2021 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#include "drive.hpp"

#include "protocol/mi/subsystem_hs_poll.hpp"
#include "protocol/mi_msg.hpp"

#include <phosphor-logging/log.hpp>
#include <regex>

using nvmemi::Drive;
using nvmemi::thresholds::Threshold;

static constexpr double nvmeTemperatureMin = -60.0;
static constexpr double nvmeTemperatureMax = 127.0;

static std::vector<Threshold> getDefaultThresholds()
{
    using nvmemi::thresholds::Direction;
    using nvmemi::thresholds::Level;
    // Using hardcoded values temporarily.
    std::vector<Threshold> thresholds{
        Threshold(Level::critical, Direction::high, 115.0),
        Threshold(Level::critical, Direction::low, 0.0),
        Threshold(Level::warning, Direction::high, 110.0),
        Threshold(Level::warning, Direction::low, 5.0)};
    return thresholds;
}

Drive::Drive(const std::string& driveName, mctpw::eid_t eid,
             sdbusplus::asio::object_server& objServer,
             std::shared_ptr<mctpw::MCTPWrapper> wrapper) :
    name(std::regex_replace(driveName, std::regex("[^a-zA-Z0-9_/]+"), "_")),
    subsystemTemp(objServer, driveName + "_Temp", getDefaultThresholds(),
                  nvmeTemperatureMin, nvmeTemperatureMax),
    mctpEid(eid), mctpWrapper(wrapper)
{
}

template <typename It>
static std::string getHexString(It begin, It end)
{
    std::stringstream ss;
    while (begin < end)
    {
        ss << std::hex << std::setfill('0') << std::setw(2)
           << static_cast<int>(*begin) << ' ';
        begin++;
    }
    return ss.str();
}

void Drive::pollSubsystemHealthStatus(boost::asio::yield_context yield)
{
    using Message = nvmemi::protocol::ManagementInterfaceMessage<uint8_t*>;
    using DWord1 = nvmemi::protocol::subsystemhs::RequestDWord1;
    static constexpr size_t reqSize =
        Message::minSize + sizeof(Message::CRC32C);
    std::vector<uint8_t> reqBuffer(reqSize, 0x00);
    nvmemi::protocol::ManagementInterfaceMessage reqMsg(reqBuffer);
    reqMsg.setMiOpCode(nvmemi::protocol::MiOpCode::subsystemHealthStatusPoll);
    auto dword1 = reinterpret_cast<DWord1*>(reqMsg.getDWord1());
    dword1->clearStatus = false;
    reqMsg.setCRC();
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        getHexString(reqBuffer.begin(), reqBuffer.end()).c_str());

    auto [ec, response] = mctpWrapper->sendReceiveYield(
        yield, this->mctpEid, reqBuffer, hsPollTimeout);
    if (ec)
    {
        phosphor::logging::log<phosphor::logging::level::ERR>(
            "Poll Subsystem health status error",
            phosphor::logging::entry("MSG=%s", ec.message().c_str()));
        return;
    }
    phosphor::logging::log<phosphor::logging::level::DEBUG>(
        getHexString(response.begin(), response.end()).c_str());

    // TODO Add sensor update
}
