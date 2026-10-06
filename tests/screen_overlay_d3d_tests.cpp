#include "d3d11_renderer.hpp"
#include "openrc/screen_overlay.hpp"
#include "openrc/render_scene.hpp"
#include "openrc/scene_timeline.hpp"
#include "openrc/runtime_world_actor.hpp"
#include "openrc/runtime_player_actor.hpp"
#include "openrc/third_person_camera.hpp"
#include "openrc/render_scene_io.hpp"
#include "openrc/runtime_level_content.hpp"
#include "openrc/prepared_game_v2.hpp"
#include "openrc/actor_animation_player.hpp"

#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <chrono>

namespace {
void check(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
struct Window {
  HWND value=nullptr;
  ~Window() {if(value) DestroyWindow(value);}
};
std::vector<std::byte> read_fixture(const char* path) {
  std::ifstream input(path,std::ios::binary|std::ios::ate);
  check(bool(input),"Cannot open neutral graphical fixture");
  const auto size=input.tellg();check(size>0&&size<=256*1024*1024,"Neutral graphical fixture exceeds limit");
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));input.seekg(0);
  input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
  check(bool(input),"Truncated neutral graphical fixture");return bytes;
}
std::uint64_t qualify_overlay_layers(openrc::runtime::D3d11Renderer& renderer,
    const std::vector<std::byte>& background) {
  std::array<openrc::ScreenOverlayV1,3> layers;
  for(unsigned layer_index=0;layer_index<layers.size();++layer_index) {
    auto& layer=layers[layer_index];layer.canvas_width=layer.canvas_height=256;
    layer.updates_per_second=50;layer.coverage_denominator=std::array{128U,255U,64U}[layer_index];
    for(unsigned image_index=0;image_index<2;++image_index) {
      openrc::ScreenOverlayImageV1 image;
      image.width=image_index?96U:128U;image.height=image_index?112U:128U;
      image.rgb_coverage.resize(std::size_t(image.width)*image.height*4U);
      for(unsigned y=0;y<image.height;++y)for(unsigned x=0;x<image.width;++x) {
        const auto at=(y*image.width+x)*4U;
        for(unsigned c=0;c<3;++c)
          image.rgb_coverage[at+c]=static_cast<std::byte>((x*3U+y*7U+layer_index*83U+image_index*61U+c*47U)&255U);
        image.rgb_coverage[at+3U]=static_cast<std::byte>((x+y*5U+layer_index*31U)%(layer.coverage_denominator+1U));
      }
      layer.images.push_back(std::move(image));
    }
    layer.frames={{{{0,-11,19},{1,59,71}}},{{{1,23,-37},{0,70,115}}},{{}}};
  }
  using Selection=std::array<std::optional<std::uint32_t>,3>;
  const std::array<Selection,9> selections{{
      {0U,0U,0U},{1U,0U,1U},{std::nullopt,1U,0U},{0U,std::nullopt,1U},
      {1U,1U,std::nullopt},{2U,1U,2U},{std::nullopt,std::nullopt,std::nullopt},
      {1U,0U,1U},{0U,0U,0U}}};
  renderer.set_screen_overlay_layers(layers);
  std::uint64_t compared=0;
  std::vector<std::byte> last_expected;
  for(const auto& selection:selections) {
    renderer.set_screen_overlay_layer_frames(selection);
    auto expected=background;
    for(std::size_t i=0;i<layers.size();++i)
      if(selection[i])openrc::composite_screen_overlay_frame_v1(expected,layers[i],*selection[i]);
    check(renderer.capture_frame_rgba()==expected,"Ordered overlay layers, independent frames, or hiding changed integer pixels");
    compared+=expected.size()/4U;last_expected=std::move(expected);
  }
  auto reversed_expected=background;
  for(auto i=layers.size();i>0;--i)
    openrc::composite_screen_overlay_frame_v1(reversed_expected,layers[i-1U],0U);
  check(reversed_expected!=last_expected,"Overlay layer fixture does not distinguish submission order");
  std::reverse(layers.begin(),layers.end());
  renderer.set_screen_overlay_layers(layers);
  check(renderer.capture_frame_rgba()==reversed_expected,"Overlay layer upload did not preserve new order or default frame zero");
  compared+=reversed_expected.size()/4U;

  bool rejected=false;
  try {renderer.set_screen_overlay_layer_frames(Selection{1U,0U,99U});}
  catch(const std::out_of_range&) {rejected=true;}
  check(rejected,"Invalid layer frame was accepted");
  check(renderer.capture_frame_rgba()==reversed_expected,"Invalid later frame partially changed an earlier layer");
  rejected=false;
  try {renderer.set_screen_overlay_layer_frames(std::array<std::optional<std::uint32_t>,1>{0U});}
  catch(const std::invalid_argument&) {rejected=true;}
  check(rejected,"Incomplete layer frame selections were accepted");
  auto limits=openrc::ScreenOverlayLimitsV1{};limits.max_images=5U;
  rejected=false;
  try {renderer.set_screen_overlay_layers(layers,limits);}
  catch(const openrc::ScreenOverlayError&) {rejected=true;}
  check(rejected,"Overlay images escaped aggregate admission limits");
  limits={};limits.max_bytes=250000U;
  rejected=false;
  try {renderer.set_screen_overlay_layers(layers,limits);}
  catch(const openrc::ScreenOverlayError&) {rejected=true;}
  check(rejected,"Overlay bytes escaped aggregate admission limits");
  check(renderer.capture_frame_rgba()==reversed_expected,"Rejected overlay admission replaced active GPU layers");

  openrc::ScreenOverlayV1 many;
  many.canvas_width=many.canvas_height=256;many.updates_per_second=50;many.coverage_denominator=128;
  many.images.resize(1025U,{1U,1U,{std::byte{17},std::byte{93},std::byte{211},std::byte{128}}});
  many.frames={{{{1024U,73,59}}}};
  rejected=false;
  try {renderer.set_screen_overlay_layers(std::span(&many,1U));}
  catch(const openrc::ScreenOverlayError&) {rejected=true;}
  check(rejected,"Overlay upload silently widened default image limits");
  limits={};limits.max_images=4096U;
  renderer.set_screen_overlay_layers(std::span(&many,1U),limits);
  auto many_expected=background;
  const auto at=(59U*256U+73U)*4U;
  std::copy(many.images[0].rgb_coverage.begin(),many.images[0].rgb_coverage.end(),many_expected.begin()+at);
  many_expected[at+3U]=std::byte{255};
  check(renderer.capture_frame_rgba()==many_expected,"Explicit large overlay admission or high image index failed");
  compared+=many_expected.size()/4U;

  // Legacy calls replace the complete layer bank and retain frame selection.
  renderer.set_screen_overlay(layers[0]);renderer.set_screen_overlay_frame(1U);
  auto single_expected=background;openrc::composite_screen_overlay_frame_v1(single_expected,layers[0],1U);
  check(renderer.capture_frame_rgba()==single_expected,"Legacy overlay setter retained stale layers");
  renderer.clear_screen_overlay();
  check(renderer.capture_frame_rgba()==background,"Legacy clear retained an overlay layer");
  renderer.set_screen_overlay_layers(layers);
  renderer.set_screen_overlay_layers({});renderer.set_screen_overlay_layer_frames({});
  check(renderer.capture_frame_rgba()==background,"Empty layer admission did not clear overlays");
  return compared;
}
void capture_prepared_environment(openrc::runtime::D3d11Renderer& renderer,HWND window,char** paths) {
  const auto limits=openrc::game::make_runtime_level_content_limits_v1();
  const auto package=openrc::parse_level_package_v1(read_fixture(paths[1]),
      {256U*1024U*1024U,32,16,256,1024,256U*1024U*1024U,256U*1024U*1024U,64});
  const auto payload=[&](const char* id,const char* type)->std::span<const std::byte> {
    for(const auto& r:package.resources) if(r.resource_id==id) {
      check(r.type_id==type&&r.schema_version==1,"Neutral graphical resource type differs");return r.payload;
    }
    throw std::runtime_error("Missing neutral graphical resource");
  };
  auto scene=openrc::decode_render_scene_v1(read_fixture(paths[2]),limits.render_scene);
  auto terrain=openrc::decode_render_scene_v1(read_fixture(paths[3]),limits.render_scene);
  const auto texture_base=static_cast<std::uint32_t>(scene.textures.size());
  const auto material_base=static_cast<std::uint32_t>(scene.materials.size());
  const auto mesh_base=static_cast<std::uint32_t>(scene.meshes.size());
  const auto instance_base=static_cast<std::uint32_t>(scene.instances.size());
  for(auto& t:terrain.textures) {t.id+=texture_base;scene.textures.push_back(std::move(t));}
  for(auto& m:terrain.materials) {m.id+=material_base;if(m.base_color_texture_id)*m.base_color_texture_id+=texture_base;scene.materials.push_back(std::move(m));}
  for(auto& m:terrain.meshes) {m.id+=mesh_base;for(auto& d:m.draw_ranges)d.material_id+=material_base;scene.meshes.push_back(std::move(m));}
  for(auto& i:terrain.instances) {i.id+=instance_base;i.mesh_id+=mesh_base;scene.instances.push_back(i);}
  const auto timeline=openrc::decode_scene_timeline_v1(payload("frontend/background/timeline","openrc.scene-timeline"));
  const auto actors=openrc::decode_actor_library_v1(payload("frontend/background/actors","openrc.actor-library"),limits.actor_library);
  const auto animation=openrc::decode_actor_animation_bank_v1(payload("frontend/background/animation","openrc.actor-animation-bank"),limits.actor_animation);
  openrc::validate_scene_timeline_bindings_v1(timeline,actors,animation);
  const auto& sample=timeline.samples.at(openrc::scene_timeline_sample_index_v1(timeline,100));
  std::vector<openrc::game::RuntimeWorldActorResolutionV1> bindings;
  for(std::size_t i=0;i<timeline.actors.size();++i) {
    const auto& a=timeline.actors[i];const auto& s=sample.actors[i];
    bindings.push_back({static_cast<std::uint32_t>(i),a.rig_index,a.model_index,a.model_to_entity,s.transform,s.enabled});
  }
  check(SetWindowPos(window,nullptr,0,0,1280,720,SWP_NOZORDER|SWP_NOACTIVATE)!=0,"Cannot resize diagnostic window");
  renderer.resize(1280,720);renderer.set_scene_geometry(scene);renderer.set_scene_actors(actors,bindings);
  renderer.set_scene_camera(sample.camera,timeline.display_aspect_numerator,timeline.display_aspect_denominator,timeline.clear_color);
  for(std::size_t i=0;i<sample.actors.size();++i) {
    const auto& a=sample.actors[i];if(!a.enabled)continue;
    const auto& rig=actors.rigs[timeline.actors[i].rig_index];const auto& clip=animation.clips[a.clip_index];
    auto playback=openrc::start_actor_animation_playback_v1(clip);playback.frame_index=a.frame_index;playback.phase=a.phase;
    renderer.set_world_actor_pose(static_cast<std::uint32_t>(i),openrc::sample_actor_animation_pose_v1(
        clip,playback,rig.semantic_key,rig.rig,{1024,1024,1024,1.e-8,1.e6F}));
  }
  const auto title=openrc::decode_screen_overlay_v1(payload("frontend/title","openrc.screen-overlay"));
  renderer.set_screen_overlay(title);renderer.set_screen_overlay_frame(openrc::screen_overlay_frame_index_v1(title,100));
  const auto pixels=renderer.capture_frame_rgba();check(pixels.size()==1280U*720U*4U,"Neutral environment capture dimensions differ");
  std::ofstream output(paths[4],std::ios::binary|std::ios::trunc);output<<"P6\n1280 720\n255\n";
  for(std::size_t i=0;i<pixels.size();i+=4)output.write(reinterpret_cast<const char*>(pixels.data()+i),3);
  output.close();check(bool(output),"Cannot write neutral environment capture");
  std::cout<<"Prepared environment GPU capture: instances="<<scene.instances.size()<<" actors="<<bindings.size()<<" update=100\n";
}
}
int main(int argc,char** argv) try {
  check(argc==1||argc==5,"Expected shared package, sky scene, terrain scene and output PPM");
  WNDCLASSW klass{};klass.lpfnWndProc=DefWindowProcW;
  klass.hInstance=GetModuleHandleW(nullptr);klass.lpszClassName=L"OpenRCScreenCompositionQualification";
  check(RegisterClassW(&klass)!=0,"window registration failed");
  // Hidden diagnostic window. Backbuffer readback is valid before Present,
  // including when the desktop reports an occluded swap chain.
  Window window{CreateWindowExW(0,klass.lpszClassName,L"OpenRC screen composition qualification",
      WS_POPUP,0,0,256,256,nullptr,nullptr,klass.hInstance,nullptr)};
  check(window.value!=nullptr,"window creation failed");
  openrc::runtime::D3d11Renderer renderer(window.value);
  renderer.resize(256,256);
  if(argc==5) {capture_prepared_environment(renderer,window.value,argv);return 0;}
  std::vector<std::byte> background(256U*256U*4U);
  openrc::ScreenOverlayV1 overlay;overlay.canvas_width=256;overlay.canvas_height=256;
  overlay.updates_per_second=50;overlay.coverage_denominator=128;
  overlay.images.push_back({256,256,std::vector<std::byte>(background.size())});
  overlay.frames={{{{0,0,0}}},{{{0,-73,51},{0,81,-43}}}};
  std::uint64_t compared=0;
  for(const auto coverage:std::array{0U,1U,17U,63U,64U,65U,95U,127U,128U}) {
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
      const auto offset=(y*256U+x)*4U;
      for(unsigned channel=0;channel<3;++channel) {
        background[offset+channel]=static_cast<std::byte>((x+channel*53U)&255U);
        overlay.images[0].rgb_coverage[offset+channel]=static_cast<std::byte>((y+channel*31U)&255U);
      }
      background[offset+3]=std::byte{255};
      overlay.images[0].rgb_coverage[offset+3]=static_cast<std::byte>(coverage);
    }
    renderer.set_media_frame(256,256,background,1,1);
    renderer.set_screen_overlay(overlay);
    for(std::uint32_t frame=0;frame<2;++frame) {
      renderer.set_screen_overlay_frame(frame);
      auto expected=background;openrc::composite_screen_overlay_frame_v1(expected,overlay,frame);
      const auto actual=renderer.capture_frame_rgba();
      check(actual.size()==expected.size(),"GPU capture has wrong dimensions");
      for(std::size_t i=0;i<actual.size();++i) if(actual[i]!=expected[i]) {
        std::cerr<<"coverage="<<coverage<<" frame="<<frame<<" byte="<<i
                 <<" expected="<<std::to_integer<unsigned>(expected[i])
                 <<" actual="<<std::to_integer<unsigned>(actual[i])<<'\n';
        throw std::runtime_error("GPU integer composition differs from CPU");
      }
      compared+=actual.size()/4U;
    }
  }
  renderer.clear_screen_overlay();
  check(renderer.capture_frame_rgba()==background,"clearing overlay changed media colors");
  const auto layered_pixels=qualify_overlay_layers(renderer,background);
  // The same device switches from a movie to authored static geometry. Test
  // actual textured pixels, replacement, visibility and retirement, rather
  // than treating submitted draw calls as evidence of a visible environment.
  openrc::RenderSceneV1 scene;
  openrc::RenderSceneTextureV1 texture;
  texture.mips={{1,1,{std::byte{255},std::byte{0},std::byte{0},std::byte{255}}}};
  scene.textures={texture};
  openrc::RenderSceneMaterialV1 material;material.base_color_texture_id=0;
  scene.materials={material};
  openrc::RenderSceneMeshV1 mesh;
  mesh.vertices={{-1,2,-1,0,0,0xffffffffU},{1,2,-1,1,0,0xffffffffU},{0,2,1,0.5F,1,0xffffffffU}};
  mesh.triangle_indices={0,1,2};mesh.draw_ranges={{0,0,3}};
  scene.meshes={mesh};scene.instances={{0,0,{}}};
  openrc::SceneCameraV1 camera;
  camera.tangent_half_horizontal=camera.tangent_half_vertical=1;
  camera.near_plane=0.1F;camera.far_plane=10;
  renderer.set_scene_camera(camera,1,1,{0,0,0,1});
  const auto check_center=[&](std::array<std::byte,4> expected) {
    const auto pixels=renderer.capture_frame_rgba();
    for(std::size_t channel=0;channel<4;++channel)
      check(pixels[(128U*256U+128U)*4U+channel]==expected[channel],"static scene pixel differs");
  };
  renderer.set_scene_geometry(scene);
  check_center({std::byte{255},std::byte{0},std::byte{0},std::byte{255}});
  check(renderer.last_frame_render_instance_submitted(0),"static instance was not submitted");
  scene.textures[0].mips[0].rgba8={std::byte{0},std::byte{255},std::byte{0},std::byte{255}};
  renderer.set_scene_geometry(scene);
  check_center({std::byte{0},std::byte{255},std::byte{0},std::byte{255}});
  renderer.set_render_instance_enabled(0,false);
  check_center({std::byte{0},std::byte{0},std::byte{0},std::byte{255}});
  check(!renderer.last_frame_render_instance_submitted(0),"hidden static instance was submitted");
  renderer.set_scene_geometry({});
  check_center({std::byte{0},std::byte{0},std::byte{0},std::byte{255}});
  openrc::ActorLibraryV1 library;
  openrc::ActorRigAssetV1 rig;rig.semantic_key="diagnostic/rig";rig.rig.joints.push_back({});
  library.rigs.push_back(rig);
  openrc::ActorModelV1 model;model.semantic_key="diagnostic/model";model.rig_key=rig.semantic_key;
  model.materials.push_back({});
  openrc::ActorSkinnedMeshV1 actor_mesh;
  for(const auto& source:mesh.vertices) {
    openrc::ActorSkinnedVertexV1 vertex;vertex.x=source.x;vertex.y=source.y;vertex.z=source.z;
    vertex.skin.influence_count=1;vertex.skin.weight_sum=1;vertex.skin.weight_numerators[0]=1;
    actor_mesh.vertices.push_back(vertex);
  }
  actor_mesh.triangle_indices=mesh.triangle_indices;actor_mesh.draw_ranges=mesh.draw_ranges;
  model.meshes.push_back(actor_mesh);library.models.push_back(model);
  openrc::game::RuntimeWorldActorResolutionV1 actor;actor.authored_id=17;actor.initially_enabled=true;
  renderer.set_scene_actors(library,std::span(&actor,1));
  check_center({std::byte{255},std::byte{255},std::byte{255},std::byte{255}});
  auto object_camera=camera;object_camera.position[0]=10;
  renderer.set_world_actor_camera(17,&object_camera);
  check_center({std::byte{0},std::byte{0},std::byte{0},std::byte{255}});
  // Global static geometry remains visible through its own camera.
  renderer.set_scene_geometry(scene);
  check_center({std::byte{0},std::byte{255},std::byte{0},std::byte{255}});
  renderer.set_scene_geometry({});
  renderer.set_world_actor_camera(17,nullptr);
  check_center({std::byte{255},std::byte{255},std::byte{255},std::byte{255}});
  renderer.set_media_frame(256,256,background,1,1);
  check(renderer.capture_frame_rgba()==background,"return from scene to media changed colors");
  renderer.set_scene_actors({},{});
  openrc::RenderSceneV1 encoded;
  openrc::RenderSceneMeshV1 quad;
  quad.vertices={{-2,2,2,0,0,0x80808080U},{2,2,2,1,0,0x80808080U},
                 {-2,2,-2,0,1,0x80808080U},{2,2,-2,1,1,0x80808080U}};
  quad.triangle_indices={0,1,2,2,1,3,0,1,2,2,1,3};
  quad.draw_ranges={{0,0,6},{1,6,6}};
  encoded.meshes={quad};encoded.instances={{0,0,{}}};
  openrc::RenderSceneMaterialV1 integer_material;
  integer_material.base_color_texture_id=0;
  integer_material.color_math=openrc::RenderSceneColorMathV1::encoded_integer;
  integer_material.interpolation=openrc::RenderSceneInterpolationV1::affine;
  integer_material.depth_test=openrc::RenderSceneDepthTestV1::always;
  integer_material.depth_write=false;integer_material.texture_modulation_denominator=128;
  integer_material.address_u=integer_material.address_v=openrc::RenderSceneAddressModeV1::clamp_to_edge;
  encoded.materials={integer_material,integer_material};
  encoded.materials[1].id=1;encoded.materials[1].base_color_texture_id=1;
  encoded.materials[1].blend_mode=openrc::RenderSceneBlendModeV1::source_over;
  encoded.materials[1].blend_denominator=128;
  openrc::RenderSceneTextureV1 dst_texture,src_texture;
  dst_texture.mips={{256,256,background}};src_texture.id=1;
  src_texture.mips={{256,256,std::vector<std::byte>(background.size())}};
  encoded.textures={dst_texture,src_texture};
  std::uint64_t encoded_pixels=0;
  for(const auto coverage:std::array{0,1,17,63,64,65,95,127,128,129,192,255}) {
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) {
      const auto at=(y*256U+x)*4U;
      for(unsigned c=0;c<3;++c) {
        encoded.textures[0].mips[0].rgba8[at+c]=static_cast<std::byte>((x+c*53)&255);
        encoded.textures[1].mips[0].rgba8[at+c]=static_cast<std::byte>((y+c*31)&255);
      }
      encoded.textures[0].mips[0].rgba8[at+3]=std::byte{128};
      encoded.textures[1].mips[0].rgba8[at+3]=static_cast<std::byte>(coverage);
    }
    renderer.set_scene_geometry(encoded);
    const auto pixels=renderer.capture_frame_rgba();
    for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) for(unsigned c=0;c<3;++c) {
      const int old=(x+c*53)&255,source=(y+c*31)&255;
      const int weighted=source*coverage+old*(128-coverage);
      const int expected=std::clamp(weighted>=0?weighted/128:-((-weighted+127)/128),0,255);
      const auto actual=std::to_integer<int>(pixels[(y*256U+x)*4U+c]);
      if(actual!=expected) {
        std::cerr<<"encoded coverage="<<coverage<<" pixel="<<x<<','<<y<<" c="<<c<<" expected="<<expected<<" actual="<<actual<<'\n';
        throw std::runtime_error("Encoded scene composition differs from integer oracle");
      }
    }
    encoded_pixels+=256U*256U;
  }
  const auto before_camera_move=renderer.capture_frame_rgba();
  encoded.instances[0].camera_relative=true;encoded.instances[0].project_to_far_plane=true;
  auto sky_camera=camera;sky_camera.position={100,50,-20};sky_camera.far_plane=1;
  renderer.set_scene_geometry(encoded);renderer.set_scene_camera(sky_camera,1,1,{0,0,0,1});
  check(renderer.capture_frame_rgba()==before_camera_move,"camera-relative far-plane image changed after camera translation");
  renderer.set_scene_camera(camera,1,1,{0,0,0,1});
  encoded.instances[0].camera_relative=false;encoded.instances[0].project_to_far_plane=false;
  encoded.meshes[0].triangle_indices.insert(encoded.meshes[0].triangle_indices.end(),{0,1,2,2,1,3});
  encoded.meshes[0].draw_ranges[1].index_count=12;
  for(std::size_t at=3;at<background.size();at+=4) encoded.textures[1].mips[0].rgba8[at]=std::byte{64};
  renderer.set_scene_geometry(encoded);
  const auto overlap=renderer.capture_frame_rgba();
  for(unsigned y=0;y<256;++y) for(unsigned x=0;x<256;++x) for(unsigned c=0;c<3;++c) {
    const unsigned old=(x+c*53)&255,source=(y+c*31)&255;
    const unsigned expected=(source+(source+old)/2)/2;
    check(std::to_integer<unsigned>(overlap[(y*256U+x)*4U+c])==expected,"overlapping encoded primitives used stale destination");
  }
  // Alpha failures keep RGB while suppressing depth writes. A later farther
  // surface must remain visible only at failing pixels, and an earlier nearer
  // surface must reject both passes. Test the cutoff on both neighboring bytes.
  openrc::RenderSceneV1 alpha_scene;
  auto alpha_material=integer_material;
  alpha_material.depth_test=openrc::RenderSceneDepthTestV1::less_equal;
  alpha_material.depth_write=true;
  alpha_material.alpha_mode=openrc::RenderSceneAlphaModeV1::mask;
  alpha_material.alpha_cutoff_rgba8=96;
  alpha_material.alpha_failure=openrc::RenderSceneAlphaFailureV1::rgb_only;
  alpha_material.blend_mode=openrc::RenderSceneBlendModeV1::source_over;
  alpha_material.blend_denominator=128;
  alpha_scene.materials={alpha_material,integer_material};
  alpha_scene.materials[1].id=1;alpha_scene.materials[1].base_color_texture_id=1;
  alpha_scene.materials[1].depth_test=openrc::RenderSceneDepthTestV1::less_equal;
  alpha_scene.materials[1].depth_write=true;
  openrc::RenderSceneTextureV1 red,green;green.id=1;
  red.mips={{1,1,{std::byte{200},std::byte{0},std::byte{0},std::byte{0}}}};
  green.mips={{1,1,{std::byte{0},std::byte{160},std::byte{0},std::byte{128}}}};
  alpha_scene.textures={red,green};
  auto foreground=quad;foreground.id=0;foreground.triangle_indices={0,1,2,2,1,3};
  foreground.draw_ranges={{0,0,6}};
  auto farther=foreground;farther.id=1;farther.draw_ranges={{1,0,6}};
  for(auto& v:farther.vertices) {v.x*=1.5F;v.y*=1.5F;v.z*=1.5F;}
  for(const auto alpha:std::array{0,64,95,96,97,128}) {
    alpha_scene.textures[0].mips[0].rgba8[3]=static_cast<std::byte>(alpha);
    alpha_scene.meshes={foreground,farther};alpha_scene.instances={{0,0,{}},{1,1,{}}};
    renderer.set_scene_geometry(alpha_scene);
    renderer.set_render_instance_enabled(1,false);
    check_center({static_cast<std::byte>(200*alpha/128),std::byte{0},std::byte{0},std::byte{255}});
    renderer.set_render_instance_enabled(1,true);
    if(alpha<96)check_center({std::byte{0},std::byte{160},std::byte{0},std::byte{255}});
    else check_center({static_cast<std::byte>(200*alpha/128),std::byte{0},std::byte{0},std::byte{255}});
    // Move the second surface in front and submit it first. Failure must
    // preserve the depth comparison, as well as suppressing depth writes.
    for(auto& v:alpha_scene.meshes[1].vertices) {v.x/=3;v.y/=3;v.z/=3;}
    alpha_scene.instances[0].mesh_id=1;alpha_scene.instances[1].mesh_id=0;
    renderer.set_scene_geometry(alpha_scene);
    check_center({std::byte{0},std::byte{160},std::byte{0},std::byte{255}});
  }
  // Vary homogeneous depth while retaining the same screen corners. Color
  // gradients stay affine even when UV uses perspective correction. Constant
  // alpha128 must remain exactly opaque at every pixel, not drift to127.
  openrc::RenderSceneV1 gradient;
  gradient.materials={integer_material};
  gradient.materials[0].blend_denominator=128;
  openrc::RenderSceneTextureV1 neutral_white;
  neutral_white.mips={{1,1,{std::byte{128},std::byte{128},std::byte{128},std::byte{128}}}};
  gradient.textures={neutral_white};
  auto gradient_mesh=foreground;
  const std::array depth_scale{1.0F,3.0F,2.0F,4.0F};
  const std::array colors{0x80200000U,0x80200080U,0x80208000U,0x80208080U};
  for(std::size_t i=0;i<4;++i) {
    auto& v=gradient_mesh.vertices[i];
    v.x*=depth_scale[i];v.y*=depth_scale[i];v.z*=depth_scale[i];v.rgba8=colors[i];
  }
  gradient.meshes={gradient_mesh};gradient.instances={{0,0,{}}};
  for(const auto interpolation:{openrc::RenderSceneInterpolationV1::perspective,openrc::RenderSceneInterpolationV1::affine}) {
    gradient.materials[0].interpolation=interpolation;
    gradient.materials[0].blend_mode=openrc::RenderSceneBlendModeV1::source_over;
    gradient.materials[0].blend_denominator=128;
    renderer.set_scene_geometry(gradient);
    const auto blended=renderer.capture_frame_rgba();
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
      const auto at=(y*256U+x)*4U;
      check(blended[at]==static_cast<std::byte>(x/2)&&blended[at+1]==static_cast<std::byte>(y/2)&&
          blended[at+2]==std::byte{32},"Encoded vertex color is not an affine byte-space gradient");
    }
    gradient.materials[0].blend_mode=openrc::RenderSceneBlendModeV1::opaque;
    gradient.materials[0].blend_denominator=255;
    renderer.set_scene_geometry(gradient);
    check(renderer.capture_frame_rgba()==blended,"Constant source alpha128 differs from opaque draw");
  }
  // Small ordered rectangles exercise destination copies away from the
  // origin and a second primitive reading the first one's updated pixels.
  auto rectangles=gradient;
  rectangles.materials[0].blend_mode=openrc::RenderSceneBlendModeV1::source_over;
  rectangles.materials[0].blend_denominator=128;
  rectangles.meshes.clear();rectangles.instances.clear();
  for(unsigned i=0;i<2;++i) {
    auto rectangle=foreground;rectangle.id=i;
    for(unsigned v=0;v<4;++v) {
      const auto x=64U+i*32U+(v%2U)*64U,y=64U+(v/2U)*64U;
      rectangle.vertices[v].x=(float(x)/128-1)*2;
      rectangle.vertices[v].y=2;rectangle.vertices[v].z=(1-float(y)/128)*2;
      rectangle.vertices[v].rgba8=i?0x40008000U:0x40000080U;
    }
    rectangles.meshes.push_back(rectangle);rectangles.instances.push_back({i,i,{}});
  }
  renderer.set_scene_geometry(rectangles);
  const auto rectangle_pixels=renderer.capture_frame_rgba();
  for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x) {
    const bool first=x>=64&&x<128&&y>=64&&y<128,second=x>=96&&x<160&&y>=64&&y<128;
    const auto at=(y*256U+x)*4U;
    check(rectangle_pixels[at]==static_cast<std::byte>(first?(second?32:64):0)&&
        rectangle_pixels[at+1]==static_cast<std::byte>(second?64:0)&&rectangle_pixels[at+2]==std::byte{0},
        "Bounded destination copy changed ordered overlapping rectangle pixels");
  }
  const auto first_drain=renderer.begin_submission_drain();
  const auto completed_drain=renderer.begin_submission_drain();
  bool stale_rejected=false;
  try{(void)renderer.submission_drain_completed(first_drain);}catch(const std::invalid_argument&){stale_rejected=true;}
  check(stale_rejected,"GPU completion accepted a superseded event token");
  const auto drain_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
  while(!renderer.submission_drain_completed(completed_drain)) {
    check(std::chrono::steady_clock::now()<drain_deadline,"GPU completion event did not finish");
    Sleep(1);
  }
  check(renderer.submission_drain_completed(completed_drain),"GPU completion event regressed");
  check(renderer.submission_drain_covers_current_work(completed_drain),"Observed scene drain did not cover its final work");
  bool stale_coverage_rejected=false;
  try{(void)renderer.submission_drain_covers_current_work(first_drain);}catch(const std::invalid_argument&){stale_coverage_rejected=true;}
  check(stale_coverage_rejected,"GPU coverage accepted a superseded event");
  // First gameplay admission uses this same renderer after a real cinematic
  // actor and overlay have occupied it. Their visibility/camera must retire.
  renderer.set_scene_actors(library,std::span(&actor,1));
  renderer.set_world_actor_camera(actor.authored_id,&object_camera);
  openrc::ScreenOverlayV1 cover;
  cover.canvas_width=cover.canvas_height=cover.updates_per_second=1;
  cover.coverage_denominator=128;
  cover.images.push_back({1,1,{std::byte{12},std::byte{50},std::byte{90},std::byte{64}}});
  cover.frames.push_back({{{0,0,0}}});renderer.set_screen_overlay(cover);
  const auto retirement_rejected=[&](std::uint64_t token) {
    bool rejected=false;
    try{renderer.retire_scene_for_media(token);}catch(const std::logic_error&){rejected=true;}
    check(rejected,"Cinematic retirement accepted missing, stale, unobserved or superseded GPU completion");
  };
  const auto retirement_try_rejected=[&](std::uint64_t token) {
    bool rejected=false;
    try{(void)renderer.try_retire_scene_for_media(token);}catch(const std::logic_error&){rejected=true;}
    check(rejected,"Cinematic retirement retry accepted an invalid token or owner state");
  };
  const auto completion_retirement_rejected=[&](std::uint64_t token) {
    bool rejected=false;
    try{(void)renderer.try_retire_submission_drain(token);}catch(const std::logic_error&){rejected=true;}
    check(rejected,"Completion owner retirement accepted a missing, stale or unobserved event");
  };
  const auto await_drain=[&] {
    const auto token=renderer.begin_submission_drain();
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
    while(!renderer.submission_drain_completed(token)) {
      check(std::chrono::steady_clock::now()<deadline,"Cinematic retirement GPU drain did not finish");
      Sleep(1);
    }
    return token;
  };
  retirement_rejected(completed_drain); // A live cinematic is not a frozen frame.
  retirement_try_rejected(completed_drain);
  const auto frozen_scene=renderer.capture_frame_rgba();
  renderer.set_media_frame(256,256,frozen_scene,1,1);
  retirement_rejected(0);
  retirement_try_rejected(0);
  completion_retirement_rejected(0);
  completion_retirement_rejected(first_drain);
  const auto unobserved=renderer.begin_submission_drain();
  bool unobserved_coverage_rejected=false;
  try{(void)renderer.submission_drain_covers_current_work(unobserved);}catch(const std::logic_error&){unobserved_coverage_rejected=true;}
  check(unobserved_coverage_rejected,"GPU coverage accepted unobserved completion");
  retirement_rejected(unobserved); // No completion has been observed, even on a fast GPU.
  retirement_try_rejected(unobserved);
  completion_retirement_rejected(unobserved);
  check(renderer.world_actor_enabled(actor.authored_id),"Rejected retirement changed cinematic actor ownership");
  const auto before_new_upload=await_drain();
  retirement_rejected(unobserved);
  retirement_try_rejected(unobserved);
  renderer.set_media_frame(256,256,frozen_scene,1,1);
  check(!renderer.submission_drain_covers_current_work(before_new_upload),"GPU coverage ignored a later upload");
  retirement_rejected(before_new_upload);
  check(!renderer.try_retire_scene_for_media(before_new_upload),"Retirement retry ignored a later upload");
  check(!renderer.try_retire_submission_drain(before_new_upload),"Completion retirement ignored a later upload");
  const auto before_new_draw=await_drain();
  (void)renderer.capture_media_frame_rgba();
  check(!renderer.submission_drain_covers_current_work(before_new_draw),"GPU coverage ignored a later paint draw");
  retirement_rejected(before_new_draw);
  check(!renderer.try_retire_scene_for_media(before_new_draw),"Retirement retry ignored a later paint draw");
  check(!renderer.try_retire_submission_drain(before_new_draw),"Completion retirement ignored a later paint draw");
  const auto before_resize=await_drain();
  renderer.resize(240,240);
  check(!renderer.submission_drain_covers_current_work(before_resize),"GPU coverage ignored a later resize");
  check(!renderer.try_retire_scene_for_media(before_resize),"Retirement retry ignored later resize work");
  check(!renderer.try_retire_submission_drain(before_resize),"Completion retirement ignored later resize work");
  check(renderer.world_actor_enabled(actor.authored_id)&&renderer.render_instance_enabled(0),
        "Retirement retry changed scene owners before the latest work completed");
  renderer.set_screen_overlay_frame(0);
  renderer.resize(256,256);
  const auto retirement_drain=await_drain();
  check(renderer.submission_drain_covers_current_work(retirement_drain),"Fresh drain did not cover final resized media work");
  const auto media_frames_before_retirement=renderer.media_frames_submitted();
  check(renderer.try_retire_scene_for_media(retirement_drain),"Fresh observed GPU drain could not retire cinematic storage");
  retirement_rejected(retirement_drain); // Retirement consumes its eligibility.
  retirement_try_rejected(retirement_drain);
  check(renderer.media_frames_submitted()==media_frames_before_retirement,"Cinematic retirement submitted a replacement frame");
  bool retired_actor=false,retired_geometry=false,retired_overlay=false;
  try{(void)renderer.world_actor_enabled(actor.authored_id);}catch(const std::out_of_range&){retired_actor=true;}
  try{(void)renderer.render_instance_enabled(0);}catch(const std::out_of_range&){retired_geometry=true;}
  try{renderer.set_screen_overlay_frame(0);}catch(const std::out_of_range&){retired_overlay=true;}
  check(retired_actor&&retired_geometry&&retired_overlay,"Cinematic retirement retained old scene owners");
  check(renderer.capture_media_frame_rgba()==frozen_scene,"Cinematic retirement changed the frozen media image");
  renderer.set_screen_overlay(cover);
  check(renderer.capture_media_frame_rgba()!=frozen_scene,"Overlay buffers were not recreated after cinematic retirement");
  renderer.clear_screen_overlay();
  check(renderer.capture_media_frame_rgba()==frozen_scene,"Clearing a new overlay changed the retained media frame");
  // Final transition cleanup releases its completed event, preserving the
  // existing image and shared renderer for later presentation/gameplay.
  const auto cleanup_drain=await_drain();
  const auto frames_before_cleanup=renderer.media_frames_submitted();
  check(renderer.try_retire_submission_drain(cleanup_drain),"Final completion owner did not retire");
  completion_retirement_rejected(cleanup_drain);
  bool retired_completion_rejected=false,retired_coverage_rejected=false;
  try{(void)renderer.submission_drain_completed(cleanup_drain);}catch(const std::invalid_argument&){retired_completion_rejected=true;}
  try{(void)renderer.submission_drain_covers_current_work(cleanup_drain);}catch(const std::invalid_argument&){retired_coverage_rejected=true;}
  check(retired_completion_rejected&&retired_coverage_rejected,"Retired completion identity remained usable");
  check(renderer.media_frames_submitted()==frames_before_cleanup,"Completion retirement submitted another frame");
  check(renderer.capture_media_frame_rgba()==frozen_scene,"Completion retirement changed the retained media frame");
  const auto after_cleanup=await_drain();
  check(after_cleanup>cleanup_drain&&renderer.submission_drain_covers_current_work(after_cleanup),
        "Renderer could not track fresh work after completion retirement");
  // The same device now admits the actual prepared player after retirement.
  openrc::game::RuntimePlayerActorResolutionV1 player;
  renderer.set_gameplay_scene({},library,player,{});
  bool old_actor_retired=false;
  try{(void)renderer.world_actor_enabled(actor.authored_id);}catch(const std::out_of_range&){old_actor_retired=true;}
  check(old_actor_retired,"Gameplay admission retained a cinematic actor");
  openrc::game::ThirdPersonCameraViewV1 gameplay_camera;
  gameplay_camera.target={0,1,0};gameplay_camera.vertical_field_of_view_radians=1.5707963267948966;
  gameplay_camera.aspect_ratio=1;gameplay_camera.near_plane_distance=0.01;gameplay_camera.far_plane_distance=100;
  renderer.set_gameplay_presentation(gameplay_camera,{0,0,0},0,0.25,1);
  check_center({std::byte{255},std::byte{255},std::byte{255},std::byte{255}});
  renderer.set_gameplay_presentation(gameplay_camera,{100,0,0},0,0.25,1);
  const auto moved_player=renderer.capture_frame_rgba();
  check(moved_player[(128U*256U+128U)*4U]!=std::byte{255},"Admitted player model ignored its gameplay transform");
  bool second_admission_rejected=false;
  try{renderer.set_gameplay_scene({},library,player,{});}catch(const std::logic_error&){second_admission_rejected=true;}
  check(second_admission_rejected,"Initial gameplay admission accepted a second active session");
  renderer.set_media_frame(256,256,frozen_scene,1,1);
  const auto gameplay_drain=await_drain();
  retirement_rejected(gameplay_drain); // Media admission cannot disguise active gameplay.
  retirement_try_rejected(gameplay_drain);
  std::cout<<"D3D11 integer screen composition: exact_pixels="<<compared
           <<" negative-clipping=pass ordered-overlap=pass clear=pass ordered-layers=pass independent-layer-frames=pass layer-hide=pass aggregate-overlay-limits=pass legacy-overlay=pass layered_pixels="<<layered_pixels
           <<" static-scene-transition=pass actor-camera=pass alpha-failure-depth=pass affine-byte-color=pass submission-drain=pass completion-owner-retirement=pass cinematic-retirement=pass retirement-retry-after-work=pass frozen-image=pass gameplay-admission=pass encoded_scene_pixels="<<encoded_pixels<<'\n';
  return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
