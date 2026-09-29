const app = getApp();

Page({
  data: {
    devices: [],
    isAdaperOpened: false,
    selectedDeviceId: null,
  },

  /**
   * 页面加载时, 打开蓝牙适配器.
   */
  onLoad(options) {
    const that = this;
    wx.openBluetoothAdapter({
      success: function () {
        that.setData({
          isAdaperOpened: true
        });
      },
      fail: function () {
        that.setData({
          isAdaperOpened: false,
        });
        wx.showToast({
          title: '未开启蓝牙权限',
          icon: 'error',
        });
      }
    });
  },

  /**
   * 开始设备扫描.
   */
  userStartDeviceScan(event) {
    const that = this;

    // 开始新的扫描
    wx.startBluetoothDevicesDiscovery({
      success: function () {
        wx.onBluetoothDeviceFound((result) => {
          const oldDeviceList = that.data.devices;
          for (let i = 0; i < result.devices.length; i++) {
            oldDeviceList.push(result.devices[i]);
          }
          that.setData({
            devices: oldDeviceList,
          });
        });
        setTimeout(() => {
          wx.stopBluetoothDevicesDiscovery({});
        }, 60000);
      },
      fail: function () {
        wx.showToast({
          title: "扫描设备失败, 请重试...",
          icon: "error",
        });
      }
    });
  },

  /**
   * 停止扫描设备.
   */
  userStopDeviceScan(event) {
    wx.stopBluetoothDevicesDiscovery({});
  },

  /**
   * 用户选择设备, 建立蓝牙连接.
   */
  userSelectDevice(event) {
    const selectedDeviceId = event.currentTarget.id

    // 断开之前连接的设备
    const oldConnectedDevice = app.globalData.connectedDevice;
    if (oldConnectedDevice != null) {
      const oldConnectedDeviceId = oldConnectedDevice.deviceId;
      // 否则断开之前的连接
      wx.closeBLEConnection({
        deviceId: oldConnectedDevice.deviceId,
        success: function (res) {
          console.log("断开连接: " + oldConnectedDevice.deviceId);
        }
      });

      if (selectedDeviceId == oldConnectedDeviceId) {
        app.globalData.connectedDevice = null;
        this.setData({
          selectedDeviceId: null,
        });
        return;
      }
    }

    const that = this;
    const devices = that.data.devices;

    // 根据deviceId查找设备
    var targetDevice = null;
    for (var dev of devices) {
      if (dev.deviceId == selectedDeviceId) {
        targetDevice = dev;
        break;
      }
    }

    // 连接到选取的设备
    wx.createBLEConnection({
      deviceId: targetDevice.deviceId,
      timeout: 2000,
      success: function (res) {
        app.globalData.connectedDevice = targetDevice;
        that.setData({
          selectedDeviceId: targetDevice.deviceId,
        });
        console.log("连接成功: " + targetDevice.deviceId);
      },
      fail: function (res) {
        app.globalData.connectedDevice = null;
        console.log("连接失败: " + targetDevice.deviceId);
      },
    });
  },
});