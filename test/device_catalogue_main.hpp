
bool assetMatches(const String& pattern,const String& name){String translated;int flags;assert(compileAssetPattern(pattern,translated,flags).empty());regex_t compiled;assert(regcomp(&compiled,translated.c_str(),flags)==0);bool match=regexec(&compiled,name.c_str(),0,nullptr,0)==0;regfree(&compiled);return match;}
int main(int argc,char**argv){
 assert(hubArtworkUrl("fw/cards/test.w640.webp")=="https://thefirmwarehub.com/fw/cards/test.w640.webp");
 assert(hubArtworkUrl("fw/cards/test.PNG")=="https://thefirmwarehub.com/fw/cards/test.PNG");
 assert(hubArtworkUrl("https://other.invalid/image.png").empty());
 assert(hubArtworkUrl("fw/cards/../private.png").empty());
 assert(hubArtworkUrl("fw/cards/nested/private.png").empty());
 assert(hubArtworkUrl("fw/cards/test.svg").empty());
 assert(assetMatches(R"(hortator-[\d.]+\.fwsc)","hortator-1.2.fwsc"));
 assert(!assetMatches(R"(hortator-[\d.]+\.fwsc)","prefix-hortator-1.2.fwsc"));
 assert(!assetMatches(R"(hortator-[\d.]+\.fwsc)","hortator-d.fwsc"));
 assert(assetMatches(R"(bocjuhn-[\w.-]+\.fwsc)","bocjuhn-v1_a.fwsc"));
 assert(assetMatches(R"(bb1-[\d.]+(?:-id)?\.fwsc)","bb1-1.2-id.fwsc"));
 assert(assetMatches(R"(LVM-Wanderer-[\d.]+(?:-RC\d+)?-FM1\.fwsc)","LVM-Wanderer-1.2-RC2-FM1.fwsc"));
 assert(assetMatches(R"((?i)keystep[-_]one[-_v][\d.]+.*\.fwsc)","KEYSTEP_ONE_1.2-extra.fwsc"));
 String rejected;int flags;assert(!compileAssetPattern("(?=evil)",rejected,flags).empty());assert(!compileAssetPattern(R"(asset-\1)",rejected,flags).empty());

 assert(argc==3);size_t expected=std::stoul(argv[2]);std::ifstream input(argv[1]);assert(input.good());String data((std::istreambuf_iterator<char>(input)),{});JsonDocument sourceDoc;assert(!deserializeJson(sourceDoc,data));size_t checkedPatterns=0;for(JsonObject item:sourceDoc["firmwares"].as<JsonArray>()){String pattern=item["assetPkg"]|"";if(pattern.empty())continue;String translated;int flags;assert(compileAssetPattern(pattern,translated,flags).empty());regex_t compiled;assert(regcomp(&compiled,translated.c_str(),flags)==0);regfree(&compiled);checkedPatterns++;}File raw(data),out("");
 auto error=normalize(raw,out,[]{return UINT64_MAX;});assert(error.empty());out.seek(0);size_t count=0,external=0;std::vector<uint32_t> offsets;
 error=scanCatalogue(out,[&](uint32_t offset,JsonObject e){count++;offsets.push_back(offset);external+=e["external_only"].as<bool>();});assert(error.empty());assert(count==expected);
 out.seek(0);CatalogueMetadata metadata;
 assert(scanCatalogue(out,[](uint32_t,JsonObject){},&metadata).empty());
 assert(metadata.sourceUrl==HUB&&metadata.license=="CC-BY-4.0"&&!metadata.retrievedAt.empty());
 size_t sourceIndex=0;
 for(auto offset:offsets){out.seek(offset);JsonDocument entry;assert(!deserializeJson(entry,out));assert(entry["name"].is<const char*>());
  JsonObject original=sourceDoc["firmwares"][sourceIndex++];
  if(original["installs"].is<uint32_t>()&&original["installs"].as<uint32_t>()<=2147483647u)assert(entry["hubInstalls"]==original["installs"]);
  else assert(entry["hubInstalls"].isNull());
  String card=original["card"]|"";String artwork=hubArtworkUrl(card);
  if(artwork.empty())assert(entry["artworkUrl"].isNull());else assert(entry["artworkUrl"]==artwork);
 }
 File duplicate("{\"schemaVersion\":1,\"entries\":[{\"id\":\"x\",\"name\":\"X\"},{\"id\":\"x\",\"name\":\"X\"}]}");assert(!scanCatalogue(duplicate,CatalogueVisitor()).empty());
 File trailing(out.bytes+"garbage");assert(!scanCatalogue(trailing,CatalogueVisitor()).empty());
 File missingSchema("{\"entries\":[]}");assert(!scanCatalogue(missingSchema,CatalogueVisitor()).empty());
 File oversized(String(1024*1024+1,' '));assert(!scanCatalogue(oversized,CatalogueVisitor()).empty());
 File reserveRaw(data),reserveOut("");assert(!normalize(reserveRaw,reserveOut,[]{return 0;}).empty());
 std::cout<<"Device catalogue: "<<count<<" entries, "<<checkedPatterns<<" compiled asset patterns, "<<external<<" external; offset lookup, duplicate IDs, trailing data, schema, size and backup reserve passed\n";
}
