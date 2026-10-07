#include "stadium_live_image_consumer.hpp"
#include <iostream>
using namespace melee_web::test::stadium_screen;
int main(){try{
    char error[256]{};
    require(melee_web_gameplay_startup(32*1024*1024,error,sizeof(error)),error);
    require(melee_web_native_world_enable(error,sizeof(error)),error);
    synthetic_checks();
    live_source_consumer_checks();
    require(melee_web_gameplay_shutdown(error,sizeof(error)),error);
    std::cout<<"Screen roots synthetic canonical IMAGE, writable SIS, catalog negatives and two lifetimes passed\n";
    std::cout<<"Live Stadium IMAGE source hit/miss/remove passed twice\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
