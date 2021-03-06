#include "config.h"
#include "sqlite_storage.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

namespace
{
    class TFakeStorage : public IStorage
    {
        WBMQTT::Testing::TLoggedFixture&      Fixture;
        std::chrono::steady_clock::time_point StartTime;
        bool                                  HasRecords;
        uint64_t                              ChannelId;

    public:
        TFakeStorage(WBMQTT::Testing::TLoggedFixture& fixture)
            : Fixture(fixture),
              StartTime(std::chrono::steady_clock::now()),
              HasRecords(false),
              ChannelId(1)
        {
        }

        PChannelInfo CreateChannel(const TChannelName& channelName) override
        {
            return CreateChannelPrivate(ChannelId++, channelName.Device, channelName.Control);
        }

        void WriteChannel(TChannelInfo&                         channelInfo,
                          const TChannel&                       channel,
                          std::chrono::system_clock::time_point time,
                          const std::string&                    groupName)
        {
            std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
            Fixture.Emit()
                << std::chrono::duration_cast<std::chrono::seconds>(now - StartTime).count();
            Fixture.Emit() << "Write \"" << groupName << "\" " << channelInfo.GetName();
            Fixture.Emit() << "  Last value: " << channel.LastValue;
            Fixture.Emit() << "  Changed: " << channel.Changed;
            Fixture.Emit() << "  Accumulator value count: " << channel.Accumulator.ValueCount;
            if (channel.Accumulator.HasValues()) {
                Fixture.Emit() << "  Sum: " << channel.Accumulator.Average();
                Fixture.Emit() << "  Min: " << channel.Accumulator.Min;
                Fixture.Emit() << "  Max: " << channel.Accumulator.Max;
            }
            HasRecords = true;
        }

        void Commit()
        {
            if (HasRecords) {
                Fixture.Emit() << "Commit";
            }
            HasRecords = false;
        }

        void GetRecords(IRecordsVisitor&                      visitor,
                        const std::vector<TChannelName>&      channels,
                        std::chrono::system_clock::time_point startTime,
                        std::chrono::system_clock::time_point endTime,
                        int64_t                               startId,
                        uint32_t                              maxRecords,
                        std::chrono::milliseconds             minInterval)
        {
        }

        void GetChannels(IChannelVisitor& visitor) {}
        void DeleteRecords(TChannelInfo& channel, uint32_t count) {}
        void DeleteRecords(const std::vector<PChannelInfo>& channels, uint32_t count) {}
    };
} // namespace

class TDBLoggerTest : public WBMQTT::Testing::TLoggedFixture
{
protected:
    WBMQTT::Testing::PFakeMqttBroker Broker;
    WBMQTT::Testing::PFakeMqttClient Client;

    std::string testRootDir;
    std::string schemaFile;

    void SetUp()
    {
        Broker = WBMQTT::Testing::NewFakeMqttBroker(*this);
        Client = Broker->MakeClient("dblogger_test");

        char* d = getenv("TEST_DIR_ABS");
        if (d != NULL) {
            testRootDir = d;
            testRootDir += '/';
        }
        testRootDir += "dblogger_test_data";

        schemaFile = testRootDir + "/../../wb-mqtt-db.schema.json";

        Client->Start();
    }
};

TEST_F(TDBLoggerTest, two_groups)
{
    TLoggerCache cache(LoadConfig(testRootDir + "/wb-mqtt-db2.conf", schemaFile).Cache);
    std::shared_ptr<TMQTTDBLogger> logger(
        new TMQTTDBLogger(Client,
                          cache,
                          std::make_unique<TFakeStorage>(*this),
                          WBMQTT::NewMqttRpcServer(Client, "db_logger"),
                          std::chrono::seconds(5)));

    auto        future = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels");
    std::thread t([=]() { logger->Start(); });
    future.Wait();
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "12.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "2.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "13.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "14.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "3.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "4.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "14.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "4.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::seconds(8));
    logger->Stop();
    t.join();
}

TEST_F(TDBLoggerTest, two_overlapping_groups)
{
    TLoggerCache cache(LoadConfig(testRootDir + "/wb-mqtt-db3.conf", schemaFile).Cache);
    std::shared_ptr<TMQTTDBLogger> logger(
        new TMQTTDBLogger(Client,
                          cache,
                          std::make_unique<TFakeStorage>(*this),
                          WBMQTT::NewMqttRpcServer(Client, "db_logger"),
                          std::chrono::seconds(5)));
    auto        future = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels");
    std::thread t([=]() { logger->Start(); });
    future.Wait();
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "12.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "2.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "13.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "14.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "3.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "4.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    Broker->Publish("test", {{"/devices/wb-adc/controls/Vin", "14.000", 1, true}});
    Broker->Publish("test", {{"/devices/wb-adc/controls/A1", "4.000", 1, true}});
    std::this_thread::sleep_for(std::chrono::seconds(8));
    logger->Stop();
    t.join();
}
