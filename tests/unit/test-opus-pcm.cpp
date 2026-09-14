/** @file tests/unit/test-opus-pcm.cpp
 * @brief Encode and decode supplied PCM without opening host audio devices.
 */
#include "../tests_common.h"
#include "src/audio.h"
#include "src/crypto.h"

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

TEST(OpusPcmTest, PreservesEveryChannelAtSupportedPacketDurations) {
  for (bool high_quality : {false, true}) {
    for (int channels : {2, 6, 8}) {
      for (int ms : {5, 10, 20}) {
        audio::config_t config {};
        config.channels = channels;
        config.packetDuration = ms;
        config.flags[audio::config_t::HIGH_QUALITY] = high_quality;
        if (high_quality && channels > 2 && ms > 5) {
          EXPECT_THROW(audio::validate_config(config), std::invalid_argument);
          EXPECT_THROW((audio::pcm_encoder(config)), std::invalid_argument);
          continue;
        }
        EXPECT_NO_THROW(audio::validate_config(config));
        audio::pcm_encoder encoder(config);
        unsigned char mapping[] {0, 1, 2, 3, 4, 5, 6, 7};
        int error = 0;
        // Standard surround couples channel pairs; high-quality surround uses independent streams.
        int coupled = channels == 2 ? 1 : high_quality ? 0 :
                                        channels == 6  ? 2 :
                                                         3;
        int streams = channels - coupled;
        auto *decoder = opus_multistream_decoder_create(48000, channels, streams, coupled, mapping, &error);
        ASSERT_EQ(error, OPUS_OK);
        auto cleanup = util::fail_guard([&] {
          opus_multistream_decoder_destroy(decoder);
        });
        int frames = ms * 48;
        std::vector<float> pcm(frames * channels), decoded(frames * channels);
        std::vector<double> energy(channels);
        // Excite one physical channel at a time. Warm past Opus's algorithmic delay.
        for (int active = 0; active < channels; ++active) {
          encoder.reset();
          opus_multistream_decoder_ctl(decoder, OPUS_RESET_STATE);
          std::fill(energy.begin(), energy.end(), 0);
          for (int block = 0; block < 5; ++block) {
            std::fill(pcm.begin(), pcm.end(), 0);
            for (int i = 0; i < frames; ++i) {
              pcm[i * channels + active] = 0.25f * std::sin((block * frames + i) * 0.12f);
            }
            auto packet = encoder.encode(pcm);
            EXPECT_LE(packet.size(), audio::max_packet_bytes);
            ASSERT_EQ(opus_multistream_decode_float(decoder, packet.begin(), packet.size(), decoded.data(), frames, 0), frames);
            if (block >= 2) {
              for (int i = 0; i < frames; ++i) {
                for (int ch = 0; ch < channels; ++ch) {
                  energy[ch] += decoded[i * channels + ch] * decoded[i * channels + ch];
                }
              }
            }
          }
          EXPECT_GT(energy[active], 0.1);
          for (int ch = 0; ch < channels; ++ch) {
            if (ch != active) {
              EXPECT_LT(energy[ch], energy[active] * 0.01 + 0.0001);
            }
          }
        }
        EXPECT_THROW(encoder.encode(std::vector<float>(1)), std::invalid_argument);
      }
    }
  }
}

TEST(OpusPcmTest, EncoderFailureDoesNotStopAnotherSessionsQueue) {
  auto previous = mail::man;
  mail::man = std::make_shared<safe::mail_raw_t>();
  auto cleanup = util::fail_guard([&] {
    mail::man = previous;
  });
  auto packets = mail::man->queue<audio::packet_t>(mail::audio_packets);
  auto samples = std::make_shared<safe::queue_t<std::vector<float>>>();
  samples->raise(std::vector<float>(1));
  audio::config_t config {};
  config.channels = 2;
  config.packetDuration = 5;
  audio::encodeThread(samples, config, nullptr);
  EXPECT_TRUE(packets->running());
}

TEST(OpusPcmTest, RejectsUnsupportedConfigurationBeforeEncoding) {
  audio::config_t config {};
  config.channels = 2;
  config.packetDuration = 7;
  EXPECT_THROW(audio::validate_config(config), std::invalid_argument);
  config.packetDuration = 5;
  config.channels = 3;
  EXPECT_THROW(audio::validate_config(config), std::invalid_argument);
}

TEST(OpusPcmTest, AlignedMaximumPacketHasRoomForFullCbcPaddingBlock) {
  crypto::aes_t key(16), iv(16);
  crypto::cipher::cbc_t cipher(key, true);
  std::string plaintext(audio::max_packet_bytes, 'a');
  std::vector<uint8_t> output(audio::max_encrypted_packet_bytes + 16, 0xa5);
  const int bytes = cipher.encrypt(plaintext, output.data(), &iv);
  ASSERT_EQ(bytes, audio::max_encrypted_packet_bytes);
  // Both the RTP and FEC headers together occupy 24 bytes.
  EXPECT_LE(bytes + 24, audio::max_datagram_bytes);
  for (size_t i = audio::max_encrypted_packet_bytes; i < output.size(); ++i) {
    EXPECT_EQ(output[i], 0xa5);
  }
}
