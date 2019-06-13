## Building the test widgets
1. Source the SDK environment script
2. Create a build directory
3. Configure and build the project

```
mkdir build
cd build
cmake .. -DBUILD_TEST_WGT=TRUE
make 
make widget
```
Note: If you omit the -DBUILD_TEST_WGT=TRUE parameter for cmake, you'll have to type `make test_widget` to compile the test widget.

This should produce two _.wgt_ files - one with the service and one with the tests within the build directory.

Run the tests:
```
scp *.wgt root@<board-ip>:~
ssh root@<board-ip>
afm-test $(ls *test.wgt)

```

The tests require actual paired and connected bluetooth-MAP enabled phone. 
WARNING: one of the tests would send actual SMS so you might want to change the phone number in test/afb-test/tests/bluetooth-map.lua
The phone currently hardcoded sends a message to +639203600900 - public sms gateway for incoming messages - https://5sim.net/free
