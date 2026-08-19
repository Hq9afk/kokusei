
#include <cassert>

#include "service/bluetooth_service.h"

void test_bluetooth() {
    using namespace bluetooth_detail;

    {
        assert(classify_icon("audio-headset") == BluetoothDeviceKind::Headset);
        assert(classify_icon("audio-headphones") ==
               BluetoothDeviceKind::Headphones);
        assert(classify_icon("input-mouse") == BluetoothDeviceKind::Mouse);
        assert(classify_icon("phone") == BluetoothDeviceKind::Phone);
        assert(classify_icon("something-unrecognized") ==
               BluetoothDeviceKind::Unknown);
    }

    {
        assert(classify_class(0x0404) == BluetoothDeviceKind::Headset);
        assert(classify_class(0x0508) == BluetoothDeviceKind::Mouse);
        assert(classify_class(0x0504) == BluetoothDeviceKind::Keyboard);
    }

    {
        BluetoothDeviceInfo connected;
        connected.connected = true;
        connected.paired = true;
        assert(is_connected_bucket(connected));
        assert(!is_paired_bucket(connected));
        assert(!is_nearby_bucket(connected));

        BluetoothDeviceInfo paired;
        paired.paired = true;
        assert(!is_connected_bucket(paired));
        assert(is_paired_bucket(paired));
        assert(!is_nearby_bucket(paired));

        BluetoothDeviceInfo trusted_only;
        trusted_only.trusted = true;
        assert(!is_connected_bucket(trusted_only));
        assert(is_paired_bucket(trusted_only));
        assert(!is_nearby_bucket(trusted_only));

        BluetoothDeviceInfo nearby;
        assert(!is_connected_bucket(nearby));
        assert(!is_paired_bucket(nearby));
        assert(is_nearby_bucket(nearby));
    }
}
