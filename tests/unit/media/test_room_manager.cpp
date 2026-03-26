#include <gtest/gtest.h>
#include "media/RoomManager.hpp"
#include "media/MockSfuBackend.hpp"

using namespace hub32api;
using namespace hub32api::media;

// ---------------------------------------------------------------------------
// Helper: builds a RoomManager backed by a MockSfuBackend
// ---------------------------------------------------------------------------
class RoomManagerTest : public ::testing::Test
{
protected:
    MockSfuBackend backend;
    RoomManager    manager{backend};
};

// ---------------------------------------------------------------------------
// GetOrCreateRouter — basic creation
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, GetOrCreateRouter_CreatesOnFirstAccess)
{
    auto result = manager.getOrCreateRouter("location-101");
    ASSERT_TRUE(result.is_ok());
    EXPECT_FALSE(result.value().empty());
    EXPECT_TRUE(manager.hasRoom("location-101"));
}

// ---------------------------------------------------------------------------
// GetOrCreateRouter — idempotent second call
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, GetOrCreateRouter_ReturnsSameOnSecondAccess)
{
    auto r1 = manager.getOrCreateRouter("location-202");
    ASSERT_TRUE(r1.is_ok());

    auto r2 = manager.getOrCreateRouter("location-202");
    ASSERT_TRUE(r2.is_ok());

    EXPECT_EQ(r1.value(), r2.value()) << "Same location must return the same routerId";
}

// ---------------------------------------------------------------------------
// GetOrCreateRouter — distinct locations get distinct routers
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, GetOrCreateRouter_DifferentLocations_DifferentRouters)
{
    auto r1 = manager.getOrCreateRouter("location-A");
    auto r2 = manager.getOrCreateRouter("location-B");
    ASSERT_TRUE(r1.is_ok());
    ASSERT_TRUE(r2.is_ok());
    EXPECT_NE(r1.value(), r2.value());
}

// ---------------------------------------------------------------------------
// DestroyRoom
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, DestroyRoom_RemovesRouter)
{
    manager.getOrCreateRouter("location-303");
    EXPECT_TRUE(manager.hasRoom("location-303"));

    manager.destroyRoom("location-303");
    EXPECT_FALSE(manager.hasRoom("location-303"));
}

// ---------------------------------------------------------------------------
// HasRoom after destroy
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, HasRoom_ReturnsFalseAfterDestroy)
{
    EXPECT_FALSE(manager.hasRoom("location-404"));

    manager.getOrCreateRouter("location-404");
    EXPECT_TRUE(manager.hasRoom("location-404"));

    manager.destroyRoom("location-404");
    EXPECT_FALSE(manager.hasRoom("location-404"));
}

// ---------------------------------------------------------------------------
// RoomCount
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, RoomCount_TracksCorrectly)
{
    EXPECT_EQ(manager.roomCount(), 0u);

    manager.getOrCreateRouter("loc-1");
    EXPECT_EQ(manager.roomCount(), 1u);

    manager.getOrCreateRouter("loc-2");
    EXPECT_EQ(manager.roomCount(), 2u);

    manager.getOrCreateRouter("loc-1"); // already exists — should not increase
    EXPECT_EQ(manager.roomCount(), 2u);

    manager.destroyRoom("loc-1");
    EXPECT_EQ(manager.roomCount(), 1u);

    manager.destroyRoom("loc-2");
    EXPECT_EQ(manager.roomCount(), 0u);
}

// ---------------------------------------------------------------------------
// GetRtpCapabilities
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, GetRtpCapabilities_ReturnsValidJson)
{
    manager.getOrCreateRouter("location-caps");

    auto result = manager.getRtpCapabilities("location-caps");
    ASSERT_TRUE(result.is_ok());

    const auto& caps = result.value();
    EXPECT_TRUE(caps.contains("codecs"));
    EXPECT_TRUE(caps["codecs"].is_array());
    EXPECT_FALSE(caps["codecs"].empty());
}

TEST_F(RoomManagerTest, GetRtpCapabilities_UnknownLocation_ReturnsError)
{
    auto result = manager.getRtpCapabilities("nonexistent-location");
    EXPECT_TRUE(result.is_err());
    EXPECT_EQ(result.error().code, ErrorCode::NotFound);
}

// ---------------------------------------------------------------------------
// Producer / Consumer counts
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, ProducerConsumerCounts_TrackCorrectly)
{
    manager.getOrCreateRouter("location-counts");

    manager.addProducer("location-counts");
    manager.addProducer("location-counts");
    manager.addConsumer("location-counts");

    auto rooms = manager.listRooms();
    ASSERT_EQ(rooms.size(), 1u);
    EXPECT_EQ(rooms[0].producerCount, 2);
    EXPECT_EQ(rooms[0].consumerCount, 1);

    manager.removeProducer("location-counts");
    rooms = manager.listRooms();
    EXPECT_EQ(rooms[0].producerCount, 1);

    manager.removeConsumer("location-counts");
    rooms = manager.listRooms();
    EXPECT_EQ(rooms[0].consumerCount, 0);
}

// ---------------------------------------------------------------------------
// ListRooms
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, ListRooms_ReturnsAllActiveRooms)
{
    manager.getOrCreateRouter("room-x");
    manager.getOrCreateRouter("room-y");
    manager.getOrCreateRouter("room-z");

    auto rooms = manager.listRooms();
    EXPECT_EQ(rooms.size(), 3u);

    manager.destroyRoom("room-y");
    rooms = manager.listRooms();
    EXPECT_EQ(rooms.size(), 2u);
}

// ---------------------------------------------------------------------------
// DestroyRoom — calling on non-existent location is a no-op
// ---------------------------------------------------------------------------

TEST_F(RoomManagerTest, DestroyRoom_NonExistentLocation_NoOp)
{
    EXPECT_NO_FATAL_FAILURE(manager.destroyRoom("never-existed"));
    EXPECT_EQ(manager.roomCount(), 0u);
}
