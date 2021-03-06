#include "config.h"
#include "sqlite_storage.h"
#include <gtest/gtest.h>
#include <stdio.h>
#include <time.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

namespace
{
    class TFakeStorage : public IStorage
    {
        WBMQTT::Testing::TLoggedFixture& Fixture;
        int Id;
    public:
        TFakeStorage(WBMQTT::Testing::TLoggedFixture& fixture) : Fixture(fixture), Id(0) {}

        PChannelInfo CreateChannel(const TChannelName& channelName) override
        {
            return CreateChannelPrivate(++Id, channelName.Device, channelName.Control);
        }

        void GetRecords(IRecordsVisitor&                      visitor,
                        const std::vector<TChannelName>&      channels,
                        std::chrono::system_clock::time_point startTime,
                        std::chrono::system_clock::time_point endTime,
                        int64_t                               startId,
                        uint32_t                              maxRecords,
                        std::chrono::milliseconds             minInterval)
        {
            Fixture.Emit() << "RPC get_records";
            for (const auto& channel : channels) {
                Fixture.Emit() << "  " << channel;
            }
            auto t1 = std::chrono::system_clock::to_time_t(startTime);
            auto t2 = std::chrono::system_clock::to_time_t(endTime);
            Fixture.Emit() << "  " << std::put_time(std::gmtime(&t1), "%Y-%m-%d %X") << " - "
                           << std::put_time(std::gmtime(&t2), "%Y-%m-%d %X");
            Fixture.Emit() << "  from " << startId;
            Fixture.Emit() << "  maxRecords " << maxRecords;
            Fixture.Emit() << "  minInterval " << minInterval.count() << " ms";
            tm dt;
            memset(&dt, 0, sizeof(tm));
            dt.tm_year = 100;
            dt.tm_mon  = 3;
            dt.tm_mday = 1;
            dt.tm_hour = 10;
            dt.tm_min  = 20;
            dt.tm_sec  = 30;
            visitor.ProcessRecord(1,
                                  1,
                                  {"wb-adc", "Vin"},
                                  "test1",
                                  std::chrono::system_clock::from_time_t(timegm(&dt)),
                                  false);
            visitor.ProcessRecord(2,
                                  1,
                                  {"wb-adc", "Vin"},
                                  10.0,
                                  std::chrono::system_clock::from_time_t(timegm(&dt)),
                                  20.0,
                                  30.0,
                                  true);
        }

        void GetChannels(IChannelVisitor& visitor)
        {
            tm dt;
            memset(&dt, 0, sizeof(tm));

            auto p = CreateChannel({"wb-adc", "Vin"});
            SetRecordCount(*p, 100);
            dt.tm_year = 100;
            dt.tm_mon  = 3;
            dt.tm_mday = 1;
            dt.tm_hour = 10;
            dt.tm_min  = 20;
            dt.tm_sec  = 30;
            SetLastRecordTime(*p, std::chrono::system_clock::from_time_t(timegm(&dt)));
            visitor.ProcessChannel(p);

            p = CreateChannel({"wb-adc",  "A1"});
            SetRecordCount(*p, 1000);
            dt.tm_year = 110;
            dt.tm_mon  = 4;
            dt.tm_mday = 2;
            dt.tm_hour = 11;
            dt.tm_min  = 21;
            dt.tm_sec  = 31;
            SetLastRecordTime(*p, std::chrono::system_clock::from_time_t(timegm(&dt)));
            visitor.ProcessChannel(p);
        }

        void WriteChannel(TChannelInfo&                         channelInfo, 
                          const TChannel&                       channelData,
                          std::chrono::system_clock::time_point time,
                          const std::string&                    groupName) {}
        void Commit() {}
        void DeleteRecords(TChannelInfo& channel, uint32_t count) {}
        void DeleteRecords(const std::vector<PChannelInfo>& channels, uint32_t count) {}
    };
} // namespace

class TRpcTest : public WBMQTT::Testing::TLoggedFixture
{
protected:
    WBMQTT::Testing::PFakeMqttBroker Broker;
    WBMQTT::Testing::PFakeMqttClient Client;

    std::string testRootDir;
    std::string schemaFile;

    void SetUp()
    {
        SetMode(E_Unordered);
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

TEST_F(TRpcTest, get_channels)
{
    TLoggerCache cache(LoadConfig(testRootDir + "/wb-mqtt-db.conf", schemaFile).Cache);
    std::shared_ptr<TMQTTDBLogger> logger(
        new TMQTTDBLogger(Client,
                          cache,
                          std::make_unique<TFakeStorage>(*this),
                          WBMQTT::NewMqttRpcServer(Client, "db_logger"),
                          std::chrono::seconds(5)));
    auto        future = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels");
    std::thread t([=]() { logger->Start(); });
    future.Wait();
    Broker->Publish("test", {{"/rpc/v1/db_logger/history/get_channels/test", "{\"id\":1}", 0, true}});
    Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels/test/reply").Wait();
    logger->Stop();
    t.join();
}

TEST_F(TRpcTest, get_records_v0)
{
    TLoggerCache cache(LoadConfig(testRootDir + "/wb-mqtt-db.conf", schemaFile).Cache);
    std::shared_ptr<TMQTTDBLogger> logger(
        new TMQTTDBLogger(Client,
                          cache,
                          std::make_unique<TFakeStorage>(*this),
                          WBMQTT::NewMqttRpcServer(Client, "db_logger"),
                          std::chrono::seconds(5)));
    auto        future  = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_values");
    auto        future2 = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels");
    std::thread t([=]() { logger->Start(); });
    future.Wait();
    future2.Wait();
    Broker->Publish("test",
                    {{"/rpc/v1/db_logger/history/get_values/test",
                      "{\"id\":1,\"params\":{\"channels\":[[\"wb-adc\",\"Vin\"],[\"wb-adc\",\"A1\"]],"
                      "\"ver\":0,\"timestamp\":{\"lt\":954566430}}}",
                      0,
                      true}});
    Broker->WaitForPublish("/rpc/v1/db_logger/history/get_values/test/reply").Wait();
    logger->Stop();
    t.join();
}

TEST_F(TRpcTest, get_records_v1)
{
    TLoggerCache cache(LoadConfig(testRootDir + "/wb-mqtt-db.conf", schemaFile).Cache);
    std::shared_ptr<TMQTTDBLogger> logger(
        new TMQTTDBLogger(Client,
                          cache,
                          std::make_unique<TFakeStorage>(*this),
                          WBMQTT::NewMqttRpcServer(Client, "db_logger"),
                          std::chrono::seconds(5)));
    auto        future  = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_values");
    auto        future2 = Broker->WaitForPublish("/rpc/v1/db_logger/history/get_channels");
    std::thread t([=]() { logger->Start(); });
    future.Wait();
    future2.Wait();
    Broker->Publish("test",
                    {{"/rpc/v1/db_logger/history/get_values/test",
                      "{\"id\":1,\"params\":{\"channels\":[[\"wb-adc\",\"Vin\"],[\"wb-adc\",\"A1\"]],"
                      "\"ver\":1,\"timestamp\":{\"lt\":954566430}}}",
                      0,
                      true}});
    Broker->WaitForPublish("/rpc/v1/db_logger/history/get_values/test/reply").Wait();
    logger->Stop();
    t.join();
}
