#include "fpga/wire.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace fpga;

namespace {
std::size_t checks = 0;
void check(bool value, const char* label) {
    ++checks;
    if (!value) throw std::runtime_error(label);
}
template<class Function> void rejects(Function function, const char* label) {
    bool caught = false;
    try { function(); } catch (const ProtocolError&) { caught = true; }
    check(caught,label);
}
std::string hex(const Bytes& bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (auto byte : bytes) out << std::setw(2) << static_cast<unsigned>(byte);
    return bytes.empty() ? "-" : out.str();
}
Bytes unhex(const char* text) {
    Bytes out;
    for (std::size_t i=0; text[i] != 0; i+=2)
        out.push_back(static_cast<std::uint8_t>(std::stoul(std::string(text+i,2),nullptr,16)));
    return out;
}
Bytes pattern(std::size_t size, std::uint32_t seed) {
    Bytes bytes(size);
    for (auto& byte : bytes) {
        seed = seed * 1664525U + 1013904223U;
        byte = static_cast<std::uint8_t>(seed >> 24);
    }
    return bytes;
}
void print_vectors() {
    const std::array<std::size_t,8> lengths{{0,1,2,4,24,384,1448,8192}};
    for (std::size_t index=0; index<command_specs().size(); ++index) {
        for (std::uint16_t dev=0;dev<=4;++dev) {
            const auto payload = pattern(lengths[(index+dev)%lengths.size()],
                                         static_cast<std::uint32_t>(index*5+dev));
            CommandFrame frame;
            frame.device=dev; frame.command=command_specs()[index].code;
            frame.message_id=static_cast<std::uint16_t>(65535-index*129-dev);
            frame.type=static_cast<std::uint8_t>((index+dev)%3);
            frame.rw=((index+dev)%2) ? rw_read : rw_write;
            frame.data=payload;
            const auto encoded=encode_command(frame);
            const auto decoded=decode_command(encoded);
            check(decoded.device==frame.device && decoded.command==frame.command &&
                  decoded.message_id==frame.message_id && decoded.type==frame.type &&
                  decoded.rw==frame.rw && decoded.data==frame.data,"differential command decode round trip");
            std::cout << "C " << frame.device << ' ' << frame.command << ' ' << frame.message_id << ' '
                      << static_cast<unsigned>(frame.type) << ' ' << static_cast<unsigned>(frame.rw) << ' '
                      << hex(payload) << ' ' << hex(encoded) << '\n';
        }
    }
    for (std::size_t index=0;index<128;++index) {
        TransportPacket packet;
        packet.device=static_cast<std::uint16_t>(index%5);
        packet.frame_id=static_cast<std::uint16_t>(index*521);
        packet.total=static_cast<std::uint16_t>(index+1);
        packet.current=static_cast<std::uint16_t>(index/2+1);
        packet.data=pattern(lengths[index%lengths.size()],static_cast<std::uint32_t>(index));
        const auto encoded=encode_transport(packet);
        const auto decoded=decode_transport(encoded);
        check(decoded.device==packet.device && decoded.frame_id==packet.frame_id && decoded.total==packet.total &&
              decoded.current==packet.current && decoded.retransmit==packet.retransmit && decoded.data==packet.data,
              "differential transport decode round trip");
        std::cout << "T " << packet.device << ' ' << packet.frame_id << ' ' << packet.total << ' '
                  << packet.current << ' ' << packet.retransmit << ' ' << hex(packet.data) << ' '
                  << hex(encoded) << '\n';
    }
    const NetworkParameters network;
    std::cout << "N " << network.device_ip << ' ' << network.host_ip << ' ' << network.mask << ' '
              << network.gateway << ' ' << hex(Bytes(network.mac.begin(),network.mac.end())) << ' '
              << hex(pack_network_parameters(network)) << '\n';
    for (const auto& cfg : {InterpolationConfig::from_mm(-50,50,0.0625,1600),
                            InterpolationConfig::from_mm(-1,1,0.01,200),
                            InterpolationConfig::from_mm(-32768,-32767,0.5,2)}) {
        std::cout << "I " << cfg.x_start << ' ' << cfg.x_end << ' ' << cfg.interval << ' ' << cfg.count << ' '
                  << hex(pack_interpolation_config(cfg)) << '\n';
    }
    std::array<double,calibration_count> values{};
    for (std::size_t i=0;i<values.size();++i) values[i]=(static_cast<double>(i)-32.0)/13.0;
    values[0]=112.817299; values[1]=101.27372; values[2]=-1; values[3]=-0.417240918;
    std::cout << "A " << std::setprecision(17);
    for (auto value : values) std::cout << value << ' ';
    std::cout << hex(pack_calibration(values)) << '\n';
    const CmosArea area{-2147483647,2147483647,-100,200};
    std::cout << "CM " << area.x1 << ' ' << area.x2 << ' ' << area.y1 << ' ' << area.y2 << ' '
              << hex(pack_cmos_area(area)) << '\n';
    for (double value : {0.0,0.02,1.0,40.0,750.0,12000.0,50000001.0}) {
        const auto ticks=sample_rate_to_ticks(value);
        std::cout << "S " << std::setprecision(17) << value << ' ' << ticks << ' '
                  << sample_ticks_to_rate(ticks) << '\n';
    }
}

void test_command_frames() {
    check(crc16_modbus(Bytes{'1','2','3','4','5','6','7','8','9'})==0x4B37,"standard CRC check value");
    check(crc16_modbus({})==0xFFFF,"empty CRC check value");
    CommandFrame command;
    command.device=1; command.command=1; command.message_id=0x1234;
    command.rw=rw_write; command.data=pack_u32(5000);
    const auto encoded=encode_command(command);
    check(encoded==unhex("aa55000100011234004000000004000013882c62fafa"),"Python reference command bytes");
    const auto decoded=decode_command(encoded);
    check(decoded.device==1 && decoded.command==1 && decoded.message_id==0x1234 && decoded.data==command.data,
          "command frame round trip");
    for (std::size_t i=0;i<encoded.size();++i) {
        auto damaged=encoded; damaged[i]^=1;
        rejects([&]{decode_command(damaged);},"every single-byte mutation must be rejected");
    }
    auto damaged=encoded; damaged[14]^=1;
    check(decode_command(damaged,false).data!=command.data,"lenient CRC option preserves damaged data for diagnostics");
    rejects([&]{decode_command(Bytes(17));},"reject short command");
    damaged=encoded; damaged[10]=0xFF;
    rejects([&]{decode_command(damaged,false);},"reject oversized declared command length without CRC");
    command.type=message_response; command.rw=rw_write; command.data={0,2};
    check(command.result_code()==2 && write_ack_result(command,1,1)==2,"write error result");
    rejects([&]{write_ack_result(command,2,1);},"reject mismatched ACK device");
    rejects([&]{write_ack_result(command,1,2);},"reject mismatched ACK command");
    command.data={0};
    rejects([&]{command.result_code();},"reject truncated ACK");
    command.command=code(Command::BatchControl);command.data={0,2,0,1};
    check(command.result_code()==2,"batch control ACK may include batch index");
    command.command=1;
    command.rw=rw_read; command.data={0,3};
    check(command.result_code()==3,"two-byte read error");
    command.command=code(Command::Heartbeat);
    check(command.result_code()==0,"heartbeat bits are not error code");
}

void test_transport_frames() {
    TransportPacket packet;
    packet.device=4;packet.frame_id=0x3210;packet.total=3;packet.current=2;packet.data=pattern(1448,23);
    const auto encoded=encode_transport(packet);
    const auto decoded=decode_transport(encoded);
    check(encoded.size()==1472 && decoded.device==4 && decoded.frame_id==0x3210 && decoded.total==3 &&
          decoded.current==2 && decoded.data==packet.data,"transport field offsets and MTU size");
    for (std::size_t i=0;i<encoded.size();++i) {
        auto damaged=encoded;damaged[i]^=1;
        rejects([&]{decode_transport(damaged);},"every transport single-byte mutation must fail");
    }
    auto bad_tail=encoded;bad_tail.back()^=1;
    check(decode_transport(bad_tail,false,true).data==packet.data,"recover valid CRC transport bad tail");
    bad_tail[20]^=1;
    rejects([&]{decode_transport(bad_tail,false,true);},"bad tail forces CRC validation");
    auto bad_length=encoded;bad_length[19]--;
    rejects([&]{decode_transport(bad_length,false);},"strict transport length validation");
    check(decode_transport(bad_length,false,false,true).data.size()==1447,"explicit Python length compatibility");
    bad_length.back()^=1;
    rejects([&]{decode_transport(bad_length,false,true,true);},"bad tail forbids length compatibility");
    packet.device=5;auto bad_device=encode_transport(packet);bad_device.back()^=1;
    rejects([&]{decode_transport(bad_device,false,true);},"bad tail rejects invalid device");
    packet.device=1;packet.reserved=1;auto bad_reserved=encode_transport(packet);bad_reserved.back()^=1;
    rejects([&]{decode_transport(bad_reserved,false,true);},"bad tail rejects reserved bits");
    packet.reserved=0;packet.total=0;
    rejects([&]{encode_transport(packet);},"reject zero normal total");
    packet.retransmit=1;packet.current=0;packet.data=pack_u16(3);
    check(decode_transport(encode_transport(packet)).retransmit==1,"retransmit request structure");
    const auto data=pattern(8192,17);
    const auto packets=split_transport(data,3,65535,1448);
    Bytes joined;
    for (std::size_t i=0;i<packets.size();++i) {
        const auto fragment=decode_transport(packets[i]);
        check(fragment.current==i+1 && fragment.total==6 && fragment.frame_id==65535 && fragment.device==3,
              "fragment sequence fields");
        joined.insert(joined.end(),fragment.data.begin(),fragment.data.end());
    }
    check(joined==data,"fragment payload round trip");
    check(split_transport({},1,0).size()==1,"empty payload is one packet");
    rejects([&]{split_transport({},0,0,0);},"reject zero fragment size");
    rejects([&]{split_transport({},0,0,65536);},"reject excessive fragment size");
    rejects([&]{split_transport(Bytes(65536),0,0,1);},"reject excessive fragment count");
}

void test_parameters() {
    check(command_specs().size()==42,"all Python commands and two documented HDR commands represented");
    for (const auto& spec : command_specs()) {
        check(command_spec(spec.code)==&spec,"command metadata lookup");
        if (spec.read_only) rejects([&]{validate_write(spec.code,{});},"read-only command rejected");
        else rejects([&]{validate_write(spec.code,Bytes(spec.write_size+1));},"wrong register width rejected");
    }
    check(default_read_payload(0x0007)==Bytes({0,1}),"camera firmware request special byte");
    check(default_read_payload(0x0204)==Bytes({0,50}),"batch polling request special count");
    check(default_read_payload(0x0205)==Bytes({0,1}),"all batch read request");
    check(default_read_payload(0x0302).empty(),"centerline empty request");
    check(camera_data_request_payload(true)==Bytes({0,0}) && camera_data_request_payload(false)==Bytes({0,1}),
          "image rearrangement request");
    validate_write(0x0003,pack_roi(2,14));
    rejects([&]{validate_write(0x0003,pack_roi(2,15));},"ROI lane bound");
    rejects([&]{validate_write(0x000F,pack_u32(101));},"laser brightness bound");
    validate_write(0x000F,pack_u32(100));
    validate_write(0x000A,pack_u32(0));
    check(sample_rate_to_ticks(0)==0 && sample_rate_to_ticks(750)==66666,"sample rate integer period convention");
    check(sample_ticks_to_rate(100000000)==0.5,"sub-Hz sample read keeps fractional rate");
    rejects([&]{sample_rate_to_ticks(-1);},"negative rate rejected");
    rejects([&]{sample_rate_to_ticks(std::numeric_limits<double>::quiet_NaN());},"NaN rate rejected");
    rejects([&]{sample_rate_to_ticks(1e-20);},"oversized sample count rejected");
    check(pack_exposure_delays({{100,200,300,400}})==unhex("00000064000000c80000012c00000190"),
          "exposure delay is direct 20 ns clock count");
    check(unpack_exposure_delays(pack_exposure_delays({{0,1,2,0xFFFFFFFFU}}))[3]==0xFFFFFFFFU,
          "full exposure uint32 range");
    check(pack_batch_control(true,123)==unhex("0001007b"),"batch start/index packing");
    check(pack_laser_delay(100,200)==unhex("00000064000000c8"),"laser timing packing");
    check(pack_hdr_knees(80,100)==unhex("0000005000000064"),"HDR knee payload");
    check(pack_hdr_exposure(5000,100)==unhex("0000138800000064"),"HDR exposure payload");
    rejects([&]{pack_hdr_knees(100,80);},"HDR descending knees rejected");
    rejects([&]{pack_hdr_knees(100,201);},"HDR out-of-range knees rejected");
    const CmosArea area{-100,200,-300,400};
    const auto got=unpack_cmos_area(pack_cmos_area(area));
    check(got.x1==-100 && got.x2==200 && got.y1==-300 && got.y2==400,"signed CMOS fields");
    const auto network=pack_network_parameters({});
    check(network==unhex("c0a80a0ac0a80a64ffffff00c0a80a010000020000000001"),"network MAC high reserved bytes");
    const auto params=unpack_network_parameters(network);
    check(params.device_ip=="192.168.10.10" && params.mac[0]==2 && params.mac[5]==1,"network decode");
    auto invalid=params;invalid.device_ip="256.1.2.3";
    rejects([&]{pack_network_parameters(invalid);},"invalid IPv4 address rejected");
    invalid.device_ip="1.2.3";
    rejects([&]{pack_network_parameters(invalid);},"short IPv4 address rejected");
    check(unpack_model(pack_model("CAM-3D1"))=="CAM-3D1","model padding round trip");
    rejects([&]{pack_model("123456789");},"model overflow rejected");
}

void test_interpolation_and_calibration() {
    const auto cfg=InterpolationConfig::from_mm(-50,50,0.0625,1600);
    check(pack_interpolation_config(cfg)==unhex("ffce0000003200000000100000000640"),"signed Q16.16 interpolation vector");
    check(cfg.last_mm()==49.9375 && cfg.receive_bounds_mm()[0]==-50.03125,"interpolation sampling positions");
    check(mm_to_q16(0.5/65536)==0 && mm_to_q16(1.5/65536)==2 && mm_to_q16(-1.5/65536)==-2,
          "Python ties-to-even Q16.16 rounding");
    rejects([&]{InterpolationConfig::from_mm(-50,50,0.0625,1601);},"right-open interpolation range");
    rejects([&]{InterpolationConfig::from_mm(0,1,1.0/65536,3);},"zero-width interpolation bins rejected");
    rejects([&]{InterpolationConfig::from_mm(0,1,0.1,2049);},"hardware interpolation count limit");
    rejects([&]{mm_to_q16(32768);},"signed Q16.16 upper range rejected");
    rejects([&]{mm_to_q16(std::numeric_limits<double>::infinity());},"non-finite Q16.16 rejected");
    std::array<double,calibration_count> values{};
    values[0]=112.817299;values[1]=101.27372;values[2]=-1;values[3]=-0.417240918;
    const auto payload=pack_calibration(values);
    check(Bytes(payload.begin(),payload.begin()+24)==unhex("0a42bb80cde00935f6691b00ffe8b7891800fff6490ce268"),
          "protocol signed 48-bit calibration examples");
    const auto decoded=unpack_calibration(payload);
    check(decoded[0]==values[0] && decoded[2]==-1 && decoded[3]==values[3],"signed 48-bit calibration decode");
    values[0]=0.5/calibration_scale;values[1]=1.5/calibration_scale;
    check(read_u32(pack_calibration(values),2)==0 && pack_calibration(values)[11]==2,"calibration ties-to-even");
    values[0]=2000;
    rejects([&]{pack_calibration(values);},"calibration overflow rejected");
    values[0]=std::numeric_limits<double>::quiet_NaN();
    rejects([&]{pack_calibration(values);},"calibration NaN rejected");
    const auto file=std::filesystem::temp_directory_path()/"fpga_wire_calibration_test.xml";
    {
        std::ofstream out(file);
        out << "<?xml version=\"1.0\"?><opencv_storage><calib>";
        const std::array<std::pair<const char*,int>,8> nodes{{{"offsets_input",2},{"gains_input",2},{"mins_input",2},
            {"offsets_output",2},{"gains_output",2},{"mins_output",2},{"weight_bias_1",30},{"weight_bias_2",22}}};
        for (const auto& node:nodes) {
            out << '<' << node.first << " type_id=\"opencv-matrix\"><rows>1</rows><cols>" << node.second
                << "</cols><dt>d</dt><data>";
            for (int i=0;i<node.second;++i) out << "1 ";
            out << "</data></" << node.first << '>';
        }
        out << "</calib></opencv_storage>";
    }
    const auto xml_data=pack_calibration_xml(file.u8string());
    std::array<double,calibration_count> ones;ones.fill(1);
    check(xml_data==pack_calibration(ones),"OpenCV XML calibration loader without OpenCV dependency");
    std::filesystem::remove(file);
}
} // namespace

int main(int argc,char** argv) {
    try {
        if (argc==2 && std::string(argv[1])=="--vectors") { print_vectors(); return 0; }
        test_command_frames();test_transport_frames();test_parameters();test_interpolation_and_calibration();
        std::cout << "wire tests passed: " << checks << " checks\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "wire test failed: " << error.what() << '\n';
        return 1;
    }
}
