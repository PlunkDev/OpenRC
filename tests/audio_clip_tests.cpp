#include "openrc/audio_clip.hpp"
#include "openrc/prepared_game_v2.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

using namespace openrc;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void reject(F call){try{call();}catch(const AudioClipError&){return;}throw std::runtime_error("Invalid PCM accepted");}
void rehash(std::vector<std::byte>& bytes){const auto digest=prepared_content_sha256_v1(std::span(bytes).subspan(64));std::copy(digest.begin(),digest.end(),bytes.begin()+32);}
}
int main()try{
  AudioClipV1 clip{48000,2,{-32768,32767,-1,0,1,-1234}};
  auto bytes=encode_audio_clip_v1(clip);
  check(decode_audio_clip_v1(bytes)==clip,"PCM channel order or signed samples changed");
  check(bytes[80]==std::byte{0}&&bytes[81]==std::byte{128}&&bytes[82]==std::byte{255}&&bytes[83]==std::byte{127},"PCM byte order is not explicit little endian");
  clip.channels=1;check(decode_audio_clip_v1(encode_audio_clip_v1(clip))==clip,"Mono PCM roundtrip changed");
  clip.channels=3;reject([&]{validate_audio_clip_v1(clip);});
  clip.channels=2;clip.samples.pop_back();reject([&]{validate_audio_clip_v1(clip);});
  clip.channels=1;clip.sample_rate=0;reject([&]{validate_audio_clip_v1(clip);});
  clip.sample_rate=48000;clip.samples.clear();reject([&]{validate_audio_clip_v1(clip);});
  auto bad=bytes;bad.back()^=std::byte{1};reject([&]{(void)decode_audio_clip_v1(bad);});
  bad=bytes;bad[72]=std::byte{255};rehash(bad);reject([&]{(void)decode_audio_clip_v1(bad);});
  bad=bytes;bad[68]=std::byte{3};rehash(bad);reject([&]{(void)decode_audio_clip_v1(bad);});
  bad=bytes;bad.push_back(std::byte{0});reject([&]{(void)decode_audio_clip_v1(bad);});
  for(std::size_t size=0;size<bytes.size();++size)reject([&]{(void)decode_audio_clip_v1(std::span(bytes).first(size));});
  AudioClipLimitsV1 limits;limits.max_samples=5;reject([&]{(void)decode_audio_clip_v1(bytes,limits);});
  limits={};limits.max_bytes=79;reject([&]{(void)decode_audio_clip_v1(bytes,limits);});
  std::cout<<"Neutral PCM signed/channel/codec/bounds tests PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
