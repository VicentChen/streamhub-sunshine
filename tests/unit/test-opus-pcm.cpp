/** @file tests/unit/test-opus-pcm.cpp
 * @brief Encode and decode supplied PCM without opening host audio devices.
 */
#include "../tests_common.h"
#include "src/audio.h"

#include <cmath>
#include <opus/opus_multistream.h>
#include <thread>

TEST(OpusPcmTest, RoundTripsSuppliedStereoPcm) {
  auto previous_mail = mail::man;
  mail::man = std::make_shared<safe::mail_raw_t>();
  auto restore = util::fail_guard([&] {
    mail::man = previous_mail;
  });
  auto packets = mail::man->queue<audio::packet_t>(mail::audio_packets);
  auto samples = std::make_shared<safe::queue_t<std::vector<float>>>(30);
  audio::config_t config {};
  config.channels = 2;
  config.packetDuration = 5;
  int channel;
  std::jthread encoder(audio::encodeThread, samples, config, &channel);
  auto stop = util::fail_guard([&] {
    samples->stop();
    encoder.join();
  });
  std::vector<float> pcm(480);
  for (std::size_t i = 0; i < 240; ++i) {
    pcm[2 * i] = pcm[2 * i + 1] = 0.3f * std::sin(i * 0.1f);
  }
  samples->raise(std::move(pcm));
  const auto packet = packets->pop(std::chrono::seconds(5));
  ASSERT_TRUE(packet);
  EXPECT_EQ(packet->first, &channel);
  int error;
  const unsigned char mapping[] {0, 1};
  auto decoder = opus_multistream_decoder_create(48000, 2, 1, 1, mapping, &error);
  ASSERT_EQ(error, OPUS_OK);
  auto free_decoder = util::fail_guard([&] {
    opus_multistream_decoder_destroy(decoder);
  });
  std::vector<float> decoded(480);
  EXPECT_EQ(opus_multistream_decode_float(decoder, packet->second.begin(), packet->second.size(), decoded.data(), 240, 0), 240);
}
