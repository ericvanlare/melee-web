// Menu scene and setup exports: the disc import transaction and its upload
// allowlists, streamed source files, save-profile settings, menu preparation
// and launch, and the CSS/SSS selection drivers.
#include "gameplay_menu_browser_state.hpp"
using namespace melee_web_menu_browser;
namespace {
constexpr size_t kSaveProfileBytes=MELEE_WEB_SAVE_PROFILE_CARD_BYTES;
// Keep the expanded fighter/Kirby archive inventory separate from the
// original title/main route inventory below. Both are accepted at the native
// file boundary; neither list substitutes for the other's source assets.
constexpr std::array<std::string_view,379> keys={"LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat","PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","LbRf.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat","PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm","GrNBa.dat","sp_zako.hps","hyaku.hps","hyaku2.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat","PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","PlMs.dat","PlMsAJ.dat","PlMsNr.dat","PlMsRe.dat","PlMsGr.dat","PlMsBk.dat","PlMsWh.dat","EfMsData.dat","mars.ssm","GrOp.dat","old_kb.hps","pupupu.ssm","MnSlChr.usd","MnSlMap.usd","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","menu01.hps","menu3.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm","smash2.sem","main.ssm","mario.ssm","dsp_coef.bin","sislib_font.bin",
 "PlDr.dat","PlDrAJ.dat","PlDrNr.dat","PlDrRe.dat","PlDrBu.dat","PlDrGr.dat","PlDrBk.dat","drmario.ssm",
 "PlFe.dat","PlFeAJ.dat","PlFeNr.dat","PlFeRe.dat","PlFeBu.dat","PlFeGr.dat","PlFeYe.dat","EfFeData.dat","emblem.ssm",
 "PlLk.dat","PlLkAJ.dat","PlLkNr.dat","PlLkRe.dat","PlLkBu.dat","PlLkBk.dat","PlLkWh.dat",
 "PlCl.dat","PlClAJ.dat","PlClNr.dat","PlClRe.dat","PlClBu.dat","PlClWh.dat","PlClBk.dat","EfLkData.dat","link.ssm","clink.ssm",
 "PlGn.dat","PlGnAJ.dat","PlGnNr.dat","PlGnRe.dat","PlGnBu.dat","PlGnGr.dat","PlGnLa.dat","EfGnData.dat","ganon.ssm",
 "PlCa.dat","PlCaAJ.dat","PlCaNr.dat","PlCaGy.dat","PlCaRe.usd","PlCaWh.dat","PlCaGr.dat","PlCaBu.dat","EfCaData.dat","captain.ssm",
 "GrSh.dat","shrine.hps","akaneia.hps","GrIz.dat","izumi.hps",
 "PlLg.dat","PlLgAJ.dat","PlLgNr.dat","PlLgWh.dat","PlLgAq.dat","PlLgPi.dat","EfLgData.dat","luigi.ssm",
 "PlPk.dat","PlPkAJ.dat","PlPkNr.dat","PlPkRe.dat","PlPkBu.dat","PlPkGr.dat",
 "PlPc.dat","PlPcAJ.dat","PlPcNr.dat","PlPcRe.dat","PlPcBu.dat","PlPcGr.dat",
 "EfPkData.dat","pikachu.ssm","pichu.ssm","GrOy.dat","old_ys.hps",
 "PlPr.dat","PlPrAJ.dat","PlPrNr.dat","PlPrRe.dat","PlPrBu.dat","PlPrGr.dat","PlPrYe.dat","EfPrData.dat","purin.ssm",
 "PlDk.dat","PlDkAJ.dat","PlDkNr.dat","PlDkBk.dat","PlDkRe.dat","PlDkBu.dat","PlDkGr.dat","EfDkData.dat","dk.ssm",
 "PlKp.dat","PlKpAJ.dat","PlKpNr.dat","PlKpRe.dat","PlKpBu.dat","PlKpBk.dat","EfKpData.dat","koopa.ssm",
 "GmRst.usd",
 "SdRst.usd",
 "TyDatai.usd",
 "IfPrize.usd",
 "SdPrize.usd",
 "s_info1.hps",
 "s_info2.hps",
 "s_info3.hps",
 "GmRstMMr.dat",
 "GmRstMDr.dat",
 "GmRstMFx.dat",
 "GmRstMFc.dat",
 "GmRstMMs.dat",
 "GmRstMFe.dat",
 "GmRstMLk.dat",
 "GmRstMCl.dat",
 "GmRstMCa.dat",
 "GmRstMDk.dat",
 "GmRstMGn.dat",
 "GmRstMKp.dat",
 "GmRstMLg.dat",
 "GmRstMMt.dat",
 "GmRstMPk.dat",
 "GmRstMPc.dat",
 "GmRstMPr.dat","GmRstMGw.dat",
 "ff_mario.hps",
 "ff_fox.hps",
 "ff_emb.hps",
 "ff_link.hps",
 "ff_fzero.hps",
 "ff_dk.hps",
 "ff_poke.hps","GmRstMNs.dat","GmRstMPe.dat","ff_nes.hps",
 "PlMt.dat","PlMtAJ.dat","PlMtNr.dat","PlMtRe.dat","PlMtBu.dat","PlMtGr.dat","EfMtData.dat","mewtwo.ssm",
 "PlNs.dat",
 "PlNsAJ.dat",
 "PlNsNr.dat",
 "PlNsYe.dat",
 "PlNsBu.dat",
 "PlNsGr.dat",
 "EfNsData.dat",
 "ness.ssm",
 "PlPe.dat",
 "PlPeAJ.dat",
 "PlPeNr.dat",
 "PlPeYe.dat",
 "PlPeWh.dat",
 "PlPeBu.dat",
 "PlPeGr.dat",
 "EfPeData.dat",
 "peach.ssm",
 "PlKb.dat","PlKbAJ.dat","PlKbNr.dat","PlKbYe.dat","PlKbBu.dat","PlKbRe.dat",
 "PlKbGr.dat","PlKbWh.dat","EfKbData.dat","kirby.ssm","GmRstMKb.dat",
 // Kirby's source copy move and hat roots, plus copy-specific effect banks.
 "PlKbCpMr.dat","PlKbCpFx.dat","PlKbCpCa.dat","PlKbCpDk.dat","PlKbCpKp.dat",
 "PlKbCpLk.dat","PlKbCpSk.dat","PlKbCpNs.dat","PlKbCpPe.dat","PlKbCpPp.dat",
 "PlKbCpPk.dat","PlKbCpSs.dat","PlKbCpYs.dat","PlKbCpPr.dat","PlKbCpMt.dat",
 "PlKbCpLg.dat","PlKbCpMs.dat","PlKbCpZd.dat","PlKbCpCl.dat","PlKbCpDr.dat",
 "PlKbCpFc.dat","PlKbCpPc.dat","PlKbCpGw.dat","PlKbCpGn.dat","PlKbCpFe.dat",
 "PlKbNrCpDk.dat","PlKbNrCpPr.dat","PlKbNrCpMt.dat","PlKbNrCpFc.dat","PlKbNrCpGw.dat",
 "PlKbYeCpDk.dat","PlKbBuCpDk.dat","PlKbReCpDk.dat","PlKbGrCpDk.dat","PlKbWhCpDk.dat",
 "PlKbYeCpPr.dat","PlKbBuCpPr.dat","PlKbReCpPr.dat","PlKbGrCpPr.dat","PlKbWhCpPr.dat",
 "PlKbYeCpMt.dat","PlKbBuCpMt.dat","PlKbReCpMt.dat","PlKbGrCpMt.dat","PlKbWhCpMt.dat",
 "PlKbYeCpFc.dat","PlKbBuCpFc.dat","PlKbReCpFc.dat","PlKbGrCpFc.dat","PlKbWhCpFc.dat",
 "EfKbMs.dat","EfKbZd.dat","EfKbMr.dat","EfKbFx.dat","EfKbSs.dat","EfKbPk.dat",
 "EfKbLg.dat","EfKbCa.dat","EfKbDk.dat","EfKbKp.dat","EfKbIc.dat","EfKbGn.dat","EfKbFe.dat",
 "samus.ssm","yoshi.ssm","zs.ssm","GmRstMSs.dat","GmRstMZd.dat","GmRstMSk.dat",
 "PlGw.dat","PlGwAJ.dat","PlGwNr.dat","gw.ssm",
 "PlSs.dat","PlSsAJ.dat","PlSsNr.dat","PlSsPi.dat","PlSsBk.dat","PlSsGr.dat","PlSsLa.dat","EfSsData.dat",
 "PlYs.dat","PlYsAJ.dat","PlYsNr.dat","PlYsRe.dat","PlYsBu.dat","PlYsYe.dat","PlYsPi.dat","PlYsAq.dat","EfYsData.dat","GmRstMYs.dat",
 "PlZd.dat","PlZdAJ.dat","PlZdNr.dat","PlZdRe.dat","PlZdBu.dat","PlZdGr.dat","PlZdWh.dat",
 "PlSk.dat","PlSkAJ.dat","PlSkNr.dat","PlSkRe.dat","PlSkBu.dat","PlSkGr.dat","PlSkWh.dat","EfZdData.dat",
 "PlPp.dat","PlPpAJ.dat","PlPpNr.dat","PlPpGr.dat","PlPpOr.dat","PlPpRe.dat",
 "PlNn.dat","PlNnAJ.dat","PlNnNr.dat","PlNnYe.dat","PlNnAq.dat","PlNnWh.dat","EfIcData.dat","GmRstMPn.dat","ice.ssm",
 "ff_flat.hps","ff_ice.hps","ff_kirby.hps","ff_samus.hps","ff_yoshi.hps",
};
constexpr std::array<std::string_view,18> zelda_sheik_keys={
 "PlZd.dat","PlZdAJ.dat","PlZdNr.dat","PlZdRe.dat","PlZdBu.dat","PlZdGr.dat","PlZdWh.dat",
 "PlSk.dat","PlSkAJ.dat","PlSkNr.dat","PlSkRe.dat","PlSkBu.dat","PlSkGr.dat","PlSkWh.dat",
 "EfZdData.dat","GmRstMZd.dat","GmRstMSk.dat","zs.ssm",
};
// PR #96's route-specific source inventory includes title, main-menu, and all
// SSM table entries needed before those scenes can select their next route.
constexpr std::array<std::string_view,278> route_asset_keys={
 "LbBf.dat","GmPause.usd","IfAll.usd","IfCoGet.dat","SdIntro.dat","PlCo.dat","PlMr.dat",
 "PlMrNr.dat","PlMrAJ.dat","GrNLa.dat","ItCo.usd","EfMrData.dat","EfCoData.dat","PdPm.dat","LbRb.dat","LbAd.dat",
 "LbRf.dat","sp_end.hps","PlMrYe.dat","PlMrBk.dat","PlMrBu.dat","PlMrGr.dat","PlFc.dat",
 "PlFcAJ.dat","PlFcNr.dat","PlFcRe.dat","PlFcBu.dat","PlFcGr.dat","EfFxData.dat","falco.ssm",
 "GrNBa.dat","sp_zako.hps","hyaku.hps","hyaku2.hps","PlFx.dat","PlFxAJ.dat","PlFxNr.dat",
 "PlFxOr.dat","PlFxLa.dat","PlFxGr.dat","fox.ssm","GrSt.dat","ystory.hps","PlMs.dat","PlMsAJ.dat",
 "PlMsNr.dat","PlMsRe.dat","PlMsGr.dat","PlMsBk.dat","PlMsWh.dat","EfMsData.dat","mars.ssm",
 "GrOp.dat","old_kb.hps","pupupu.ssm","MnSlChr.usd","MnSlMap.usd","MnMaAll.usd","GmTtAll.usd",
 "SdMenu.usd","SdToy.dat","SdSlChr.usd","MnExtAll.usd","LbMcGame.usd","NtMemAc.usd","LbMcSnap.usd",
 "GmEvent.dat","menu01.hps","menu3.hps","nr_select.ssm","nr_title.ssm","nr_name.ssm","pokemon.ssm","end.ssm",
 "smash2.sem","main.ssm","kongo.ssm","mario.ssm","nr_1p.ssm","nr_vs.ssm","gkoopa.ssm","ice.ssm",
 "kirby.ssm","samus.ssm","zs.ssm","yoshi.ssm","gw.ssm","mhands.ssm","kirbytm.ssm","castle.ssm",
 "corneria.ssm","greatbay.ssm","mutecity.ssm","onett.ssm","zebes.ssm","garden.ssm","klaid.ssm",
 "greens.ssm","venom.ssm","bigblue.ssm","fourside.ssm","pstadium.ssm","1padv.ssm","ending.ssm",
 "1pend.ssm","last.ssm","dsp_coef.bin","sislib_font.bin","PlDr.dat","PlDrAJ.dat","PlDrNr.dat",
 "PlDrRe.dat","PlDrBu.dat","PlDrGr.dat","PlDrBk.dat","drmario.ssm","PlFe.dat","PlFeAJ.dat",
 "PlFeNr.dat","PlFeRe.dat","PlFeBu.dat","PlFeGr.dat","PlFeYe.dat","EfFeData.dat","emblem.ssm",
 "PlLk.dat","PlLkAJ.dat","PlLkNr.dat","PlLkRe.dat","PlLkBu.dat","PlLkBk.dat","PlLkWh.dat",
 "PlCl.dat","PlClAJ.dat","PlClNr.dat","PlClRe.dat","PlClBu.dat","PlClWh.dat","PlClBk.dat",
 "EfLkData.dat","link.ssm","clink.ssm","PlGn.dat","PlGnAJ.dat","PlGnNr.dat","PlGnRe.dat",
 "PlGnBu.dat","PlGnGr.dat","PlGnLa.dat","EfGnData.dat","ganon.ssm","PlCa.dat","PlCaAJ.dat",
 "PlCaNr.dat","PlCaGy.dat","PlCaRe.usd","PlCaWh.dat","PlCaGr.dat","PlCaBu.dat","EfCaData.dat",
 "captain.ssm","GrSh.dat","shrine.hps","akaneia.hps","GrIz.dat","izumi.hps","PlLg.dat",
 "PlLgAJ.dat","PlLgNr.dat","PlLgWh.dat","PlLgAq.dat","PlLgPi.dat","EfLgData.dat","luigi.ssm",
 "PlPk.dat","PlPkAJ.dat","PlPkNr.dat","PlPkRe.dat","PlPkBu.dat","PlPkGr.dat","PlPc.dat",
 "PlPcAJ.dat","PlPcNr.dat","PlPcRe.dat","PlPcBu.dat","PlPcGr.dat","EfPkData.dat","pikachu.ssm",
 "pichu.ssm","GrOy.dat","old_ys.hps","PlPr.dat","PlPrAJ.dat","PlPrNr.dat","PlPrRe.dat",
 "PlPrBu.dat","PlPrGr.dat","PlPrYe.dat","EfPrData.dat","purin.ssm","PlDk.dat","PlDkAJ.dat",
 "PlDkNr.dat","PlDkBk.dat","PlDkRe.dat","PlDkBu.dat","PlDkGr.dat","EfDkData.dat","dk.ssm",
 "PlKp.dat","PlKpAJ.dat","PlKpNr.dat","PlKpRe.dat","PlKpBu.dat","PlKpBk.dat","EfKpData.dat",
 "koopa.ssm","GmRst.usd","SdRst.usd","TyDatai.usd","TyDatai.dat","IfPrize.usd","SdPrize.usd","s_info1.hps",
 "s_info2.hps","s_info3.hps","GmRstMMr.dat","GmRstMDr.dat","GmRstMFx.dat","GmRstMFc.dat",
 "GmRstMMs.dat","GmRstMFe.dat","GmRstMLk.dat","GmRstMCl.dat","GmRstMCa.dat","GmRstMDk.dat",
 "GmRstMGn.dat","GmRstMKp.dat","GmRstMLg.dat","GmRstMMt.dat","GmRstMPk.dat","GmRstMPc.dat",
 "GmRstMPr.dat","ff_mario.hps","ff_fox.hps","ff_emb.hps","ff_link.hps","ff_fzero.hps","ff_dk.hps",
 "ff_poke.hps","GmRstMNs.dat","GmRstMPe.dat","ff_nes.hps","PlMt.dat","PlMtAJ.dat","PlMtNr.dat",
 "PlMtRe.dat","PlMtBu.dat","PlMtGr.dat","EfMtData.dat","mewtwo.ssm","PlNs.dat","PlNsAJ.dat",
 "PlNsNr.dat","PlNsYe.dat","PlNsBu.dat","PlNsGr.dat","EfNsData.dat","ness.ssm","PlPe.dat",
 "PlPeAJ.dat","PlPeNr.dat","PlPeYe.dat","PlPeWh.dat","PlPeBu.dat","PlPeGr.dat","EfPeData.dat",
 "peach.ssm"};
}
extern "C" {
unsigned melee_web_native_asset_begin(){try{
 close();scoped_disc_import=true;scoped_assets=true;
 request_assets(AssetDestination::InitialMenu);
 return asset_generation;
}catch(const std::exception& e){message=e.what();return 0;}}
unsigned melee_web_native_asset_count(unsigned generation){
 return generation&&generation==asset_scope.pending_generation()?requested_assets.size():0;
}
const char* melee_web_native_asset_name(unsigned generation,unsigned index){
 return generation&&generation==asset_scope.pending_generation()&&index<requested_assets.size()?
        requested_assets[index].c_str():nullptr;
}
int melee_web_native_asset_file(unsigned generation,const char* name,const uint8_t* data,unsigned size){try{
 check(scoped_assets&&!world&&!match&&!archive_cache&&name&&data,
       "Native asset transfer requires closed source owners");
 asset_scope.put(generation,name,{data,size});return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_asset_commit(unsigned generation){try{
 check(scoped_assets&&!world&&!match&&!results&&!prize&&!archive_cache,
      "Close source owners before asset commit");
 asset_scope.commit(generation);asset_committed=true;return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_asset_abort(unsigned generation){try{
 asset_scope.abort(generation);return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_set_save_profile(int mode,const uint8_t* data,unsigned size){try{
 check(!host&&!world&&!match&&!results&&!prize&&!host_entered,
       "Unload source owners before changing the staged save profile");
 check(mode==MELEE_WEB_SAVE_MODE_EVERYTHING||mode==MELEE_WEB_SAVE_MODE_PERSONAL,
       "Unsupported save mode");
 check((size==0&&!data)||(size==kSaveProfileBytes&&data),
       "Save profile must be empty or the exact original card-manifest extent");
 check(mode!=MELEE_WEB_SAVE_MODE_EVERYTHING||size==0,
       "Everything unlocked uses its original source baseline");
 std::vector<uint8_t> next;
 if(size)next.assign(data,data+size);
 configured_save_profile=std::move(next);configured_save_mode=mode;
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_snapshot_save_profile(uint8_t* output,unsigned size,int baseline){try{
 check(host!=nullptr,"A live source profile is unavailable for export");
 char error[256]{};
 check(melee_web_menu_host_snapshot_card_data(host,baseline,output,size,error,sizeof(error)),error);
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_snapshot_unlocked_baseline(uint8_t* output,unsigned size){try{
 check(output&&size==MELEE_WEB_SAVE_PROFILE_CARD_BYTES,
       "Baseline export requires the exact original card-manifest extent");
 if(host){
  char error[256]{};
  check(melee_web_menu_host_snapshot_card_data(host,1,output,size,error,sizeof(error)),error);
  return 1;
 }
 check(!world&&!match&&!results&&!prize&&!source_session_owned,
       "Close source owners before establishing the original save baseline");
 check(!archive_cache,
       "Release the native archive cache before establishing an unowned save baseline");
 begin_source_session();
 char error[256]{};
 MeleeWebSaveProfileOwner* profile=melee_web_save_profile_owner_create(error,sizeof(error));
 if(!profile)throw std::runtime_error(error);
 bool active=false;
 std::unique_ptr<melee_web::GameplayMenuWorld> baseline_world;
 try{
  check(melee_web_save_profile_owner_activate(profile,error,sizeof(error)),error);active=true;
  check(melee_web_save_profile_owner_initialize_default(profile,error,sizeof(error)),error);
  archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
  baseline_world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
  Toy_803124BC();
  check(melee_web_save_profile_owner_initialize_everything(profile,error,sizeof(error)),error);
  check(melee_web_save_profile_owner_snapshot_card_data(profile,output,size,error,sizeof(error)),error);
  check(melee_web_save_profile_owner_deactivate(profile,error,sizeof(error)),error);active=false;
  check(melee_web_save_profile_owner_destroy(profile,error,sizeof(error)),error);profile=nullptr;
  baseline_world->close_prepared();baseline_world.reset();
  archive_cache.reset();
  check(melee_web_gameplay_session_end(error,sizeof(error)),error);source_session_owned=false;
 }catch(...){
  if(baseline_world){
   try{baseline_world->close_prepared();}catch(...){}
   baseline_world.reset();
  }
  archive_cache.reset();
  if(active&&!melee_web_save_profile_owner_deactivate(profile,nullptr,0))std::abort();
  if(profile&&!melee_web_save_profile_owner_destroy(profile,nullptr,0))std::abort();
  if(source_session_owned){
   if(!melee_web_gameplay_session_end(nullptr,0))std::abort();
   source_session_owned=false;
  }
  throw;
 }
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_file(const char* name,const uint8_t* data,unsigned size){try{
if(scoped_assets)throw std::runtime_error("Scoped disc imports require an asset transaction");
 if(world||match||results||prize||!name||!data||!size||size>64*1024*1024)
  throw std::runtime_error("Unload before importing valid local files");
#if defined(MELEE_WEB_PUBLIC_AUDIO_DISABLED)
 if(std::string_view{name}=="dsp_coef.bin")throw std::runtime_error("Public audio-disabled runtime does not accept DSP coefficient input");
#endif
 bool known=false;
 for(auto key:keys)known|=key==name;
 for(auto key:route_asset_keys)known|=key==name;
 for(auto key:zelda_sheik_keys)known|=key==name;
 if(!known)throw std::runtime_error("Unknown native menu file: "+std::string(name));
 archive_cache.reset();
 files[name]={data,data+size};scoped_disc_import=false;return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_source_file_external_set(const char* name,unsigned size){try{
 if(world||match||results||prize||host_entered)
  throw std::runtime_error("Close native source owners before configuring streamed disc files");
 char error[256]{};
 check(melee_web_source_files_external_set(name,size,error,sizeof(error)),
       error[0]?error:"Native streamed disc file configuration failed");
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_source_files_external_clear(){try{
 if(world||match||results||prize||host_entered)
  throw std::runtime_error("Close native source owners before clearing streamed disc files");
 char error[256]{};
 check(melee_web_source_files_external_clear(error,sizeof(error)),
       error[0]?error:"Native streamed disc file catalog release failed");
 scoped_disc_import=false;
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_prepare(){try{
#if defined(MELEE_WEB_PIPELINE_PROVENANCE)
 const melee_web::provenance::Scope provenance(pipeline_context(MELEE_WEB_PIPELINE_PHASE_PREPARATION));
#endif
 if(match||results||prize||host_entered)throw std::runtime_error("Unload before preparing native menu resources");
 if(world&&host){message="Native menu resources already prepared.";return 1;}
 if(scoped_assets){
  check(asset_destination==AssetDestination::InitialMenu&&asset_committed,
        "Import the complete menu asset scope before preparation");
  asset_destination=AssetDestination::None;asset_committed=false;
 }
 if(world||host)close();
 char error[256]{};const double started=emscripten_get_now();const AuroraStats before=aurora_stats_snapshot();
 // Preserve the source ownership order used by launch: the menu host claims
 // RNG/session ownership before the SDK world is started. No source scene or
 // simulation callback runs during this preparation phase.
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 begin_source_session();
 host=melee_web_menu_host_create_with_profile(
     configured_save_mode,configured_save_profile.empty()?nullptr:configured_save_profile.data(),
     configured_save_profile.size(),error,sizeof(error));check(host!=nullptr,error);
 // One unentered menu preparation belongs to the canonical fresh import.
 // Repeating it changes allocation history even without entering a source scene.
 if(reference_menu_preparations++)reference_heap_used=true;
 world=std::make_unique<melee_web::GameplayMenuWorld>(files,*archive_cache);
 check(melee_web_menu_host_initialize_profile_baseline(host,error,sizeof(error)),error);
 const double constructed=emscripten_get_now();
 report_construction("scene-prepare",started,constructed,constructed,before,aurora_stats_snapshot());
 message="Native menu resources prepared.";return 1;
}catch(const std::exception& e){
 diagnostic_incident(4);
 if(world&&!host_entered){try{world->close_prepared();}catch(...){}}
 world.reset();
 if(host&&!host_entered){char ignored[256]{};melee_web_menu_host_destroy(host,ignored,sizeof(ignored));host=nullptr;}
 if(source_session_owned&&!world&&!host){
  char ignored[256]{};
  if(melee_web_gameplay_session_end(ignored,sizeof(ignored)))source_session_owned=false;
 }
 message=e.what();return 0;
}}
int melee_web_native_menu_launch(){try{
 if(preparation.busy()||pending)
  throw std::runtime_error("Native menu transition is still preparing");
 if(match||results||prize||host_entered||(host&&!world))close();
 if(!archive_cache)archive_cache=std::make_unique<melee_web::RuntimeArchiveCache>(files);
 begin_source_session();
 char error[256]{};if(!host){host=melee_web_menu_host_create_with_profile(
     configured_save_mode,configured_save_profile.empty()?nullptr:configured_save_profile.data(),
     configured_save_profile.size(),error,sizeof(error));check(host!=nullptr,error);}
 VISetFrameBufferScale(1);enter_world();
#if defined(MELEE_WEB_SELECTIVE_PIPELINES)
 // The initial owner has entered but has not advanced a source tick. Settle
 // its certified union through the same draw/submission gate as transitions.
 check(preparation.request_render_settle(),"Initial pipeline preparation is already active");
 preparation_profile.begin(true,emscripten_get_now());
 running=false;menu_clock.reset();
 message="Preparing original character select...";
 EM_ASM({window.menuPreparation?.(UTF8ToString($0),false);},message.c_str());
#endif
 return 1;
}catch(const std::exception& e){diagnostic_incident(4);message=e.what();running=false;return 0;}}
#if defined(MELEE_WEB_STADIUM_C1A_DIAGNOSTIC)
int melee_web_native_menu_stadium_c1a_arm(){try{
 check(!stadium_c1a_armed&&scoped_assets&&
       asset_destination==AssetDestination::None&&!asset_committed&&
       asset_scope.pending_generation()==0&&world&&host&&!host_entered&&
       !match&&!results&&!prize&&!pending&&!running&&
       melee_web_menu_host_phase(host)==MELEE_WEB_MENU_CREATED&&
       melee_web_menu_host_mode_kind(host)==GM_VS,
       "Stadium C1a arm requires one prepared imported-disc VS menu session");
 char error[256]{};
 check(melee_web_menu_host_enable_stadium_c1a(host,error,sizeof(error)),error);
 stadium_c1a_armed=true;stadium_c1a_observation.clear();
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_stadium_c1a_asset_scope(unsigned generation){
 if(!generation||generation!=asset_generation||
    generation!=asset_scope.pending_generation()||
    !stadium_c1a_armed||!scoped_assets||asset_committed||
    asset_destination!=AssetDestination::StadiumC1a||
    !asset_selection_valid||world||match||results||prize||host_entered)
  return 0;
 try{
  return requested_assets==melee_web::stadium_c1a_asset_names(asset_selection)?1:0;
 }catch(...){return 0;}
}
const char* melee_web_native_menu_stadium_c1a_observe(){
 return stadium_c1a_observation.empty()?nullptr:stadium_c1a_observation.c_str();
}
#endif
int melee_web_native_menu_drive_fighter(int character_kind){try{
 if(!host||melee_web_menu_host_phase(host)!=1)throw std::runtime_error("Fighter selection drive requires the original CSS");
 if(css_fighter_release_port>=0){
  const unsigned port=static_cast<unsigned>(css_fighter_release_port);
  check(melee_web_native_menu_pad_sample_full(port,0,0,0,0,0,0,0,1),message.c_str());
  css_fighter_release_port=-1;
  return 1;
 }
 MeleeWebFighterInputObservation observed{};check(melee_web_fighter_input_observe(character_kind,&observed),"CSS target observation is unavailable");
 last_css_fighter_observation=observed;last_css_fighter_target=character_kind;
 last_css_fighter_observation_valid=true;
 PADStatus raw[PAD_MAX_CONTROLLERS]{};const int state=melee_web_fighter_input_drive(raw,&observed,character_kind);
 last_css_fighter_drive_state=state;
 check(state!=MELEE_WEB_FIGHTER_INPUT_INVALID,"CSS target observation is invalid");
 if(state==MELEE_WEB_FIGHTER_INPUT_ALREADY_SELECTED)return 2;
 const bool pressed=state==MELEE_WEB_FIGHTER_INPUT_PICKUP_READY||
                    state==MELEE_WEB_FIGHTER_INPUT_TARGET_READY;
 if(pressed)raw[observed.cursor_port].button|=PAD_BUTTON_A;
 check(melee_web_native_menu_pad_sample_full(observed.cursor_port,raw[observed.cursor_port].button,
       raw[observed.cursor_port].stickX,raw[observed.cursor_port].stickY,
       raw[observed.cursor_port].substickX,raw[observed.cursor_port].substickY,
       raw[observed.cursor_port].triggerLeft,raw[observed.cursor_port].triggerRight,1),message.c_str());
 if(pressed)css_fighter_release_port=observed.cursor_port;
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
int melee_web_native_menu_drive_stage(int stage_kind){try{
 if(!host||melee_web_menu_host_phase(host)!=3)throw std::runtime_error("Stage selection drive requires the original SSS");
 MeleeWebStageInputObservation observed{};check(melee_web_stage_input_observe(stage_kind,&observed),"SSS target observation is unavailable");
 PADStatus raw[PAD_MAX_CONTROLLERS]{};const int state=melee_web_stage_input_drive(raw,&observed,stage_kind);
 check(state!=MELEE_WEB_STAGE_INPUT_INVALID,"SSS target observation is invalid");if(state==MELEE_WEB_STAGE_INPUT_AT_TARGET)return 2;
 check(melee_web_native_menu_pad_sample_full(0,raw[0].button,raw[0].stickX,raw[0].stickY,
       raw[0].substickX,raw[0].substickY,raw[0].triggerLeft,raw[0].triggerRight,1),message.c_str());
 return 1;
}catch(const std::exception& e){message=e.what();return 0;}}
}
