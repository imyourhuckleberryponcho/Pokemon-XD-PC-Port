#include "room_recipe.h"
#include "room_c_api.h"
#include "room_mesh.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace engine::rooms;

namespace {
int sFail = 0;
void Check(bool c, const std::string& what) {
  std::printf("%s %s\n", c ? "ok  " : "FAIL", what.c_str());
  if (!c) sFail++;
}
void Put32(std::vector<uint8_t>& d, size_t at, uint32_t v) { d[at] = uint8_t(v >> 24), d[at + 1] = uint8_t(v >> 16), d[at + 2] = uint8_t(v >> 8), d[at + 3] = uint8_t(v); }

HsdArchive TinyMap() {
  HsdArchive a;
  a.data.assign(0x1C0, 0);
  a.SetU32(0x00, 0x20), a.SetU32(0x04, 0x40), a.SetU32(0x08, 0x60);
  a.SetU32(0x20, 0x80);
  a.SetU32(0x60, 0x100);
  a.SetU32(0x80, 0xA0);
  a.SetU32(0xA0 + 8, 0x140);
  a.SetF32(0xA0 + 0x20, 1), a.SetF32(0xA0 + 0x24, 1), a.SetF32(0xA0 + 0x28, 1);
  a.SetF32(0xA0 + 0x2C, 5);
  a.SetF32(0x140 + 0x20, 1), a.SetF32(0x140 + 0x24, 1), a.SetF32(0x140 + 0x28, 1);
  for (uint32_t s : {0x00u, 0x04u, 0x08u, 0x20u, 0x60u, 0x80u, 0xA8u}) a.AddSlot(s);
  a.publics.emplace_back("scene_data", 0);
  return a;
}

std::vector<uint8_t> MakePng(int w, int h, const uint8_t* rgb) {
  std::vector<uint8_t> raw;
  for (int y = 0; y < h; y++) {
    raw.push_back(0);
    raw.insert(raw.end(), rgb + y * w * 3, rgb + (y + 1) * w * 3);
  }
  const uint16_t n = uint16_t(raw.size()), nn = uint16_t(~n);
  std::vector<uint8_t> z{0x78, 0x01, 0x01, uint8_t(n & 255), uint8_t(n >> 8), uint8_t(nn & 255), uint8_t(nn >> 8)};
  z.insert(z.end(), raw.begin(), raw.end());
  z.insert(z.end(), 4, 0);
  std::vector<uint8_t> png{0x89, 'P', 'N', 'G', 13, 10, 26, 10};
  const auto chunk = [&](const char* t, const std::vector<uint8_t>& d) {
    for (int i = 3; i >= 0; i--) png.push_back(uint8_t(d.size() >> (8 * i)));
    png.insert(png.end(), t, t + 4);
    png.insert(png.end(), d.begin(), d.end());
    png.insert(png.end(), 4, 0);
  };
  chunk("IHDR", {0, 0, 0, uint8_t(w), 0, 0, 0, uint8_t(h), 8, 2, 0, 0, 0});
  chunk("IDAT", z);
  chunk("IEND", {});
  return png;
}
std::vector<uint8_t> MakeGlb(const std::vector<float>& pos, const std::vector<float>& uv, const std::vector<uint16_t>& idx, const std::vector<uint8_t>& png) {
  std::vector<uint8_t> bin;
  const auto add = [&](const void* d, size_t n) {
    while (bin.size() % 4) bin.push_back(0);
    const size_t at = bin.size();
    bin.insert(bin.end(), static_cast<const uint8_t*>(d), static_cast<const uint8_t*>(d) + n);
    return at;
  };
  const size_t pOff = add(pos.data(), pos.size() * 4), uOff = uv.empty() ? 0 : add(uv.data(), uv.size() * 4), iOff = add(idx.data(), idx.size() * 2),
               imOff = png.empty() ? 0 : add(png.data(), png.size());
  std::string views = "{\"buffer\":0,\"byteOffset\":" + std::to_string(pOff) + ",\"byteLength\":" + std::to_string(pos.size() * 4) + "},{\"buffer\":0,\"byteOffset\":" +
                      std::to_string(iOff) + ",\"byteLength\":" + std::to_string(idx.size() * 2) + "}";
  std::string accs = "{\"bufferView\":0,\"componentType\":5126,\"count\":" + std::to_string(pos.size() / 3) + ",\"type\":\"VEC3\"},{\"bufferView\":1,\"componentType\":5123,\"count\":" +
                     std::to_string(idx.size()) + ",\"type\":\"SCALAR\"}";
  std::string attrs = "\"POSITION\":0";
  std::string extra;
  if (!uv.empty()) {
    views += ",{\"buffer\":0,\"byteOffset\":" + std::to_string(uOff) + ",\"byteLength\":" + std::to_string(uv.size() * 4) + "}";
    accs += ",{\"bufferView\":2,\"componentType\":5126,\"count\":" + std::to_string(uv.size() / 2) + ",\"type\":\"VEC2\"}";
    attrs += ",\"TEXCOORD_0\":2";
  }
  std::string mat = "\"materials\":[{\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0.5,0.25,1]";
  if (!png.empty()) {
    views += ",{\"buffer\":0,\"byteOffset\":" + std::to_string(imOff) + ",\"byteLength\":" + std::to_string(png.size()) + "}";
    mat += ",\"baseColorTexture\":{\"index\":0}";
    extra = ",\"images\":[{\"bufferView\":" + std::string(uv.empty() ? "2" : "3") + ",\"mimeType\":\"image/png\"}],\"textures\":[{\"source\":0}]";
  }
  mat += "}}]";
  std::string j = "{\"asset\":{\"version\":\"2.0\"},\"scene\":0,\"scenes\":[{\"nodes\":[0]}],\"nodes\":[{\"mesh\":0,\"translation\":[0,1,0]}],\"meshes\":[{\"primitives\":[{\"attributes\":{" +
                  attrs + "},\"indices\":1,\"material\":0}]}]," + mat + extra + ",\"accessors\":[" + accs + "],\"bufferViews\":[" + views + "],\"buffers\":[{\"byteLength\":" +
                  std::to_string(bin.size()) + "}]}";
  while (j.size() % 4) j += ' ';
  std::vector<uint8_t> out;
  const auto le = [&](uint32_t v) { for (int i = 0; i < 4; i++) out.push_back(uint8_t(v >> (8 * i))); };
  while (bin.size() % 4) bin.push_back(0);
  out.insert(out.end(), {'g', 'l', 'T', 'F'});
  le(2), le(uint32_t(12 + 8 + j.size() + 8 + bin.size()));
  le(uint32_t(j.size())), le(0x4E4F534A);
  out.insert(out.end(), j.begin(), j.end());
  le(uint32_t(bin.size())), le(0x004E4942);
  out.insert(out.end(), bin.begin(), bin.end());
  return out;
}
}

int main() {
  {
    Ccd c;
    CcdRecord r = CcdNewRecord();
    CcdTri g[2];
    CcdMakeGround(-30, -30, 30, 30, 0, g);
    r.block[kCcdWalk].tris = {g[0], g[1]};
    CcdAddBox(r, 0, 0, 5, 5, -2, 20);
    c.records.push_back(r);
    const std::vector<uint8_t> bytes = CcdWrite(c);
    std::vector<std::string> p;
    Check(CcdValidate(bytes.data(), bytes.size(), p), "a collision file written by the library passes the check");
    Ccd back;
    std::string why;
    Check(CcdRead(bytes.data(), bytes.size(), back, why) && back.records.size() == 1 && back.records[0].block[kCcdHit].tris.size() == 8, "it reads back with its 8 wall triangles");
    Check(CcdWrite(back) == bytes, "written again it is byte for byte the same");
    std::vector<uint8_t> broken = bytes;
    Put32(broken, 4, 65);
    p.clear();
    Check(!CcdValidate(broken.data(), broken.size(), p), "65 records are refused");
    broken = bytes;
    Put32(broken, 0x10 + 0x24, uint32_t(bytes.size() + 100));
    p.clear();
    Check(!CcdValidate(broken.data(), broken.size(), p), "a block pointer outside the file is refused");
    broken = bytes;
    broken.resize(bytes.size() - 40);
    p.clear();
    Check(!CcdValidate(broken.data(), broken.size(), p), "a file cut short is refused");
    broken = bytes;
    Put32(broken, 0x10 + 0, 0x7FC00000u);
    p.clear();
    Check(!CcdValidate(broken.data(), broken.size(), p), "NaN in a record is refused");
    CcdRemoveHitIn(back, -6, -6, 6, 6);
    Check(back.records[0].block[kCcdHit].tris.empty(), "walls can be removed by area");
  }
  {
    uint32_t seed = 12345;
    const auto rnd = [&] { seed = seed * 1664525u + 1013904223u; return seed >> 8; };
    Ccd c;
    CcdRecord r = CcdNewRecord();
    CcdTri g[2];
    CcdMakeGround(-30, -30, 30, 30, 0, g);
    r.block[kCcdWalk].tris = {g[0], g[1]};
    CcdAddBox(r, 0, 0, 5, 5, -2, 20);
    c.records.push_back(r);
    const std::vector<uint8_t> ccdBytes = CcdWrite(c), mapBytes = HsdWrite(TinyMap());
    int accepted = 0;
    for (int i = 0; i < 20000; i++) {
      std::vector<uint8_t> a = ccdBytes, b = mapBytes;
      for (int k = 0; k < 1 + int(rnd() % 4); k++) a[rnd() % a.size()] = uint8_t(rnd()), b[rnd() % b.size()] = uint8_t(rnd());
      if (i % 7 == 0) a.resize(rnd() % a.size()), b.resize(rnd() % b.size());
      std::vector<std::string> p;
      accepted += CcdValidate(a.data(), a.size(), p);
      p.clear();
      accepted += HsdValidateMap(b.data(), b.size(), p);
    }
    Check(true, "20000 damaged collision and map files were answered without a crash (" + std::to_string(accepted) + " still passed the check)");
  }
  {
    const char* abc = "abc";
    Check(RoomSha1(reinterpret_cast<const uint8_t*>(abc), 3) == "a9993e364706816aba3e25717850c26c9cd0d89d", "SHA-1 of \"abc\"");
  }
  {
    HsdArchive a = TinyMap();
    std::vector<uint8_t> bytes = HsdWrite(a);
    std::vector<std::string> p;
    SceneInfo s;
    Check(HsdValidateMap(bytes.data(), bytes.size(), p, &s), "a small made-up map passes the check");
    Check(s.models.size() == 1 && s.models[0].joints.size() == 2 && s.lights == 1 && s.camera, "its scene lists 1 model, 2 joints, 1 light, a camera");
    Check(s.models[0].joints[1].parent == 0 && s.models[0].joints[1].number == 1, "joint 1 is the child of joint 0");
    HsdArchive noCam = a;
    noCam.SetU32(0x04, 0);
    noCam.RemoveSlot(0x04);
    bytes = HsdWrite(noCam);
    p.clear();
    Check(!HsdValidateMap(bytes.data(), bytes.size(), p), "a map without a camera is refused");
    bytes = HsdWrite(a);
    HsdArchive parsed;
    p.clear();
    Check(HsdParse(bytes.data(), bytes.size(), parsed, p) && HsdWrite(parsed) == bytes, "the container reads and writes back byte for byte");
    std::vector<uint8_t> loop = bytes;
    Put32(loop, 0x20 + 0x140 + 8, 0xA0);
    HsdArchive l;
    p.clear();
    SceneInfo s2;
    Check(HsdParse(loop.data(), loop.size(), l, p) && !HsdListScene(l, s2, p), "a joint tree that loops is refused");
    std::vector<uint8_t> shortf(bytes.begin(), bytes.begin() + bytes.size() - 5);
    p.clear();
    Check(!HsdValidateMap(shortf.data(), shortf.size(), p), "a file cut short is refused");
    HsdSetTranslation(parsed, s.models[0].joints[0].offset, 1, 2, 3);
    Check(parsed.F32(0xA0 + 0x30) == 2, "a joint can be moved");
    HsdArchive donor = TinyMap();
    std::string problem;
    uint32_t root = 0, added = 0;
    Check(HsdAddPiece(parsed, donor, 0xA0, 10, 0, 20, 90, problem, &root, &added), "a piece (joint tree) of a map is copied into another: " + problem);
    const std::vector<uint8_t> out = HsdWrite(parsed);
    p.clear();
    SceneInfo s3;
    Check(HsdValidateMap(out.data(), out.size(), p, &s3) && s3.models.size() == 2 && s3.models[1].joints.size() == 2, "the result is a valid map with 2 models");
    Check(std::fabs(parsed.F32(root + 0x2C) - 10.f) < 1e-6f, "the copy stands where it was put");
  }
  {
    const std::vector<float> pos = {-5, 4, 0, -5, 0, 12, 5, 0, 12, 5, 4, 0,
                                    -5, 0, 0, -5, 4, 0, 5, 4, 0, 5, 0, 0};
    const std::vector<uint16_t> idx = {0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
    const std::vector<uint8_t> glb = MakeGlb(pos, {}, idx, {});
    MeshModel m;
    std::string why;
    Check(GltfRead(glb.data(), glb.size(), m, why) && m.prims.size() == 1 && m.prims[0].idx.size() == 12, "a .glb with one mesh is read: " + why);
    Check(m.prims[0].pos.size() == 24 && std::fabs(m.prims[0].pos[1] - 5.f) < 1e-5f, "the node's translation is applied");
    Check(std::fabs(m.materials[0].color[1] - 0.5f) < 1e-6f, "the base colour is read");
    const std::vector<uint8_t> cut(glb.begin(), glb.begin() + glb.size() / 2);
    MeshModel m2;
    Check(!GltfRead(cut.data(), cut.size(), m2, why) && !why.empty(), "a .glb cut short is refused: " + why);
    HsdArchive map = TinyMap();
    std::vector<std::string> p;
    SceneInfo sc;
    uint32_t root = 0, added = 0;
    Check(HsdAddMeshModel(map, m, 1, 90, 3, 0, 4, why, &root, &added), "the model is written into a map: " + why);
    const std::vector<uint8_t> out = HsdWrite(map);
    Check(HsdValidateMap(out.data(), out.size(), p, &sc) && sc.models.size() == 2 && sc.models[1].joints.size() == 1 && sc.models[1].dobjs == 1,
          "the map with the model passes the check (a new model, 1 joint, 1 mesh)");
    float lo[3], hi[3];
    Check(HsdSubtreeBounds(map, root, lo, hi) && std::fabs(hi[1] - 5.f) < 1e-4f && std::fabs(hi[0] - 12.f) < 1e-4f,
          "the vertices read back from the written display list are where the file put them");
    const uint8_t rgb[4 * 4 * 3] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9, 255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9,
                                    255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9, 255, 0, 0, 0, 255, 0, 0, 0, 255, 9, 9, 9};
    const std::vector<uint8_t> glb2 = MakeGlb({0, 0, 0, 4, 0, 0, 4, 4, 0, 0, 4, 0}, {0, 1, 1, 1, 1, 0, 0, 0}, {0, 1, 2, 0, 2, 3}, MakePng(4, 4, rgb));
    MeshModel t;
    Check(GltfRead(glb2.data(), glb2.size(), t, why) && t.images.size() == 1 && t.images[0].width == 4 && t.images[0].rgba[0] == 255 && t.images[0].rgba[5] == 255,
          "a picture inside the .glb (PNG) is decoded: " + why);
    HsdArchive map2 = TinyMap();
    Check(HsdAddMeshModel(map2, t, 2, 0, 0, 0, 0, why, &root, &added), "a textured model is written: " + why);
    const std::vector<uint8_t> out2 = HsdWrite(map2);
    p.clear();
    SceneInfo sc2;
    Check(HsdValidateMap(out2.data(), out2.size(), p, &sc2) && sc2.models.size() == 2 && sc2.models[1].textures == 1, "the map with the textured model passes the check (1 texture)");
    const auto tris = MeshWorldTriangles(m, 1, 0, 0, 0, 0);
    Check(tris.size() == 4, "the model's 4 triangles");
    CcdRecord rec = CcdNewRecord();
    const MeshCollisionCount mc = CcdAddMeshTriangles(rec, tris);
    Check(mc.floor == 2 && mc.walls == 2, "the slope (normal up) goes to the ground, the back to the walls");
    Check(rec.block[kCcdWalk].tris[0].n[1] > 0.9f && rec.block[kCcdHit].tris[0].n[2] < -0.9f, "the ground faces up and the back wall faces north (-z)");
    Check(rec.block[kCcdHit].tris[0].tail[1] != 0, "the free edges of a wall are tested (edge flags set)");
    const std::string rj = "{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"pieces\":[{\"gltf\":\"parts/w.glb\",\"at\":[0,0,10],\"collide\":\"mesh\"}]}";
    RoomRecipe rr;
    p.clear();
    Check(ParseRecipe(rj, rr, p) && rr.pieces.size() == 1 && rr.pieces[0].gltf == "parts/w.glb" && rr.pieces[0].collide == "mesh", "a recipe piece with \"gltf\" is read");
    const std::string again = RecipeToJson(rr);
    RoomRecipe r2;
    Check(ParseRecipe(again, r2, p) && RecipeToJson(r2) == again, "and written back");
    Check(!ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"pieces\":[{\"gltf\":\"../w.glb\",\"at\":[0,0,0]}]}", rr, p),
          "a path leading out of the room's folder is refused");
    Check(!ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"pieces\":[{\"gltf\":\"a.glb\",\"from\":\"M1\",\"joint\":1,\"at\":[0,0,0]}]}", rr, p),
          "\"gltf\" together with \"from\" is refused");
  }
  {
    std::vector<std::string> p;
    RoomRecipe r;
    Check(ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a_room\",\"base\":\"M3_houseA_1F\",\"hide\":[{\"model\":0,\"joint\":1}],\"walls\":{\"boxes\":[{\"cx\":0,\"cz\":0,\"sx\":4,\"sz\":4}]},\"entrances\":[{\"x\":0,\"y\":0,\"z\":10,\"angle\":0}]}", r, p) &&
              r.hide.size() == 1 && r.boxes.size() == 1 && r.entrances.size() == 1,
          "a good recipe is read");
    const std::string again = RecipeToJson(r);
    RoomRecipe r2;
    p.clear();
    Check(ParseRecipe(again, r2, p) && RecipeToJson(r2) == again, "a recipe written by the library reads back the same");
    p.clear();
    Check(!ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"hid\":[]}", r, p), "a misspelt field is reported, not ignored");
    p.clear();
    Check(!ParseRecipe("{\"format\":\"xd-room\",\"version\":2,\"name\":\"a\",\"base\":\"x\"}", r, p), "another format version is refused");
    p.clear();
    Check(!ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"hide\":[{\"model\":0}]}", r, p), "a hide without a joint is reported");
    HsdArchive m = TinyMap();
    Ccd c;
    CcdRecord rec = CcdNewRecord();
    CcdTri g[2];
    CcdMakeGround(-30, -30, 30, 30, 0, g);
    rec.block[kCcdWalk].tris = {g[0], g[1]};
    CcdAddWall(rec, -30, -30, 30, -30, -2, 20, false);
    c.records.push_back(rec);
    RoomInputs in;
    in.map = HsdWrite(m), in.collision = CcdWrite(c);
    RoomRecipe good;
    p.clear();
    ParseRecipe("{\"format\":\"xd-room\",\"version\":1,\"name\":\"t\",\"base\":\"b\",\"hide\":[{\"model\":0,\"joint\":1}],\"move\":[{\"model\":0,\"joint\":0,\"by\":[3,0,0]}],\"walls\":{\"add\":[{\"line\":[0,20,30,20]}],\"boxes\":[{\"cx\":0,\"cz\":0,\"sx\":4,\"sz\":4}],\"remove\":[{\"area\":[-31,-31,31,-29]}]}}", good, p);
    RoomOutputs out;
    p.clear();
    const bool ok = ApplyRecipe(good, in, out, p);
    for (const std::string& s : p) std::printf("  %s\n", s.c_str());
    Check(ok, "a recipe with hide, move, walls, a box and a removed wall is applied");
    Ccd back;
    std::string why;
    Check(ok && CcdRead(out.collision.data(), out.collision.size(), back, why) && back.records[0].block[kCcdHit].tris.size() == 2 + 8, "the wall was removed and 1 wall + 1 box added (2 + 8 triangles)");
    RoomRecipe bad = good;
    bad.hide[0].joint = 99;
    p.clear();
    RoomOutputs o2;
    Check(!ApplyRecipe(bad, in, o2, p) && !p.empty(), "a joint that does not exist is reported: " + (p.empty() ? std::string() : p[0]));
    RoomInputs big = in;
    big.maxArchiveBytes = 100, big.baseArchiveBytes = 90;
    p.clear();
    Check(!ApplyRecipe(good, big, o2, p), "a room over the size limit is refused");
    xdr_result res;
    const char* rj = "{\"format\":\"xd-room\",\"version\":1,\"name\":\"t\",\"base\":\"b\",\"walls\":{\"boxes\":[{\"cx\":0,\"cz\":0,\"sx\":4,\"sz\":4}]}}";
    Check(xdr_build(rj, in.map.data(), in.map.size(), in.collision.data(), in.collision.size(), nullptr, nullptr, 0, 0, &res) == 1 && res.map_size && res.collision_size && std::strstr(res.report_json, "\"ok\": true"), "C interface: a recipe is built");
    size_t pn = 0;
    uint8_t* png = xdr_collision_png(res.collision, res.collision_size, 64, &pn);
    Check(png && pn > 8 && png[1] == 'P', "C interface: a top view PNG");
    xdr_free(png);
    char* parts = xdr_map_parts(res.map, res.map_size, nullptr);
    Check(parts && std::strstr(parts, "\"models\""), "C interface: the joint list");
    xdr_free(parts);
    xdr_result_free(&res);
    char* chk = xdr_recipe_check("{\"format\":\"xd-room\",\"version\":1,\"name\":\"a\",\"base\":\"x\",\"hid\":[]}");
    Check(std::strstr(chk, "\"ok\": false"), "C interface: a misspelt field is reported");
    xdr_free(chk);
    Check(xdr_build("not json", in.map.data(), in.map.size(), in.collision.data(), in.collision.size(), nullptr, nullptr, 0, 0, &res) == 0 && res.report_json, "C interface: a broken recipe is refused");
    xdr_result_free(&res);
  }
  std::printf("%s\n", sFail == 0 ? "ALL OK" : "FAILED");
  return sFail == 0 ? 0 : 1;
}
