#include <fpga/device.hpp>
#include <iostream>

int main(int argc,char** argv){
    try{
        bool simulation=argc>1 && std::string(argv[1])=="--simulate";
        fpga::Simulator simulator;
        fpga::ConnectionOptions options;
        if(simulation){simulator.start();options.device_ip="127.0.0.1";options.device_port=simulator.port();options.local_ip="127.0.0.1";options.local_port=0;}
        else if(argc>1)options.device_ip=argv[1];
        fpga::Device camera;camera.open(options);
        auto heartbeat=camera.read(0,0x0401,{std::chrono::milliseconds{1000},0});
        if(!heartbeat){std::cerr<<heartbeat.error<<'\n';return 1;}
        std::cout<<"Heartbeat: 0x"<<std::hex<<fpga::read_u16(heartbeat.frame.data)<<std::dec<<'\n';
        auto exposure=camera.read(1,0x0001);
        if(!exposure){std::cerr<<exposure.error<<'\n';return 1;}
        std::cout<<"Exposure: "<<fpga::read_u32(exposure.frame.data)<<" us\n";
        auto response=camera.get_camera_data(1,true,{std::chrono::milliseconds{3000},0});
        if(!response){std::cerr<<response.error<<'\n';return 1;}
        auto frame=camera.wait_next_frame(1,0,std::chrono::milliseconds{3000});
        if(!frame||!frame->data.image){std::cerr<<"No decoded image\n";return 1;}
        std::cout<<"Image: "<<frame->data.image->width<<'x'<<frame->data.image->height<<", centerline "<<frame->data.centerline.size()<<", cloud "<<frame->data.points3d.size()<<"\n";
        camera.close();return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
