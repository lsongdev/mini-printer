const app = getApp();

const OWL_BLE_SERVICE_UUID = "4e3eeda0-83bf-45d7-8707-769d6348411c";
const OWL_BLE_DATA_INPUT_UUID = "4e3eedb1-83bf-45d7-8707-769d6348411c";
const OWL_BLE_STATUS_REPORT_UUID = "4e3eedb2-83bf-45d7-8707-769d6348411c";

Page({
  data: {
    connectedDeviceId: null,
    volt: null,
    temp: null,
    paper: null,
    deviceStatusTimerId: null,
    selectedImageFilePath: null,
    finalImageData: null,
  },

  onShow(options) {
    const that = this;
    const connectedDevice = app.globalData.connectedDevice;

    if (connectedDevice != null) {
      // 记录设备ID
      that.setData({
        connectedDeviceId: connectedDevice.deviceId,
      });
      // 注册一个特征变化的回调函数
      wx.onBLECharacteristicValueChange((result) => {
        const values = new Float32Array(result.value);

        if (values.length < 3) {
          console.log("读取设备信息失败");
          return;
        }

        const temp = values[0].toFixed(1);
        const volt = values[1].toFixed(1);
        const paper = values[2] == 0 ? "正常" : "异常";

        that.setData({
          volt: volt,
          temp: temp,
          paper: paper,
        });

        console.log("读取到设备信息: ", "volt=" + volt, "temp=" + temp, "paper=" + paper);
      });
      // 开启读取设备信息的定时器
      const timerId = setInterval(() => {
        wx.readBLECharacteristicValue({
          characteristicId: OWL_BLE_STATUS_REPORT_UUID,
          deviceId: connectedDevice.deviceId,
          serviceId: OWL_BLE_SERVICE_UUID,
        });
        console.log("开始读取设备信息...");
      }, 2000);

      that.setData({
        deviceStatusTimerId: timerId,
      });
      console.log("定时器开启");
    }
  },

  onHide(options) {
    // 关闭读取设备信息的定时器
    const that = this;
    const timerId = that.data.deviceStatusTimerId;

    if (timerId != null) {
      // 关闭监听特征的回调
      wx.offBLECharacteristicValueChange();
      // 关闭定时器
      clearInterval(timerId);
      that.setData({
        deviceStatusTimerId: null,
      });
      console.log("定时器关闭");
    }
  },

  userSelectImage(event) {
    const that = this;

    // 获取用户选择的图片路径
    wx.chooseMedia({
      count: 1,
      mediaType: ["image"],
      sourceType: ["album"],
      sizeType: ["original", "compressed"],
      success: function (res) {
        const imageFilePath = res.tempFiles[0].tempFilePath;
        that.setData({
          selectedImageFilePath: imageFilePath,
        });
        console.log("用户选择文件: " + imageFilePath);

        wx.getImageInfo({
          src: imageFilePath,
          success: function (res) {
            const imageWidth = res.width;
            const imageHeight = res.height;

            // 获取 <canvas> 元素的上下文
            const context = wx.createCanvasContext('myCanvas');

            // 获取 <canvas> 的宽度和高度
            const canvasWidth = 300;
            const canvasHeight = 300;

            // 计算缩放比例
            const scale = Math.min(canvasWidth / imageWidth, canvasHeight / imageHeight);

            // 计算绘制图像的宽度和高度
            const drawWidth = imageWidth * scale;
            const drawHeight = imageHeight * scale;

            // 计算图像在 <canvas> 中的位置，使其居中
            const x = (canvasWidth - drawWidth) / 2;
            const y = (canvasHeight - drawHeight) / 2;

            // 绘制图像
            context.drawImage(imageFilePath, x, y, drawWidth, drawHeight);

            // 将图像绘制到canvas上, 并进行灰度处理
            context.draw(false, () => {
              wx.canvasGetImageData({
                canvasId: 'myCanvas',
                height: 300,
                width: 300,
                x: 0,
                y: 0,
                success: (res) => {
                  const finalImageData = new Uint8ClampedArray(384 * 48);
                  const imageData = res.data;

                  console.log(imageData.length);

                  for (let i = 0; i < 384; i++) {
                    for (let j = 0; j < 384; j++) {
                      var ii = Math.round(300 * i / 384);
                      var jj = Math.round(300 * j / 384);

                      var gray = 0.3 * imageData[ii * 300 * 4 + jj * 4] + 0.6 * imageData[ii * 300 * 4 + jj * 4 + 1] + 0.1 * imageData[ii * 300 * 4 + jj * 4 + 2];
                      gray = (gray >= 128 ? 0 : 1);
                      var shift = j % 8;
                      gray = (gray << (7 - shift));

                      var index = i * 48 + Math.trunc(j / 8);
                      finalImageData[index] = finalImageData[index] | gray;
                    }
                  }

                  that.setData({
                    finalImageData: finalImageData,
                  });

                  wx.showToast({
                    title: '图片处理完毕',
                    icon: "success",
                  });
                },
                fail: (res) => {
                  wx.showToast({
                    title: '图片处理失败',
                    icon: "error",
                  });
                  console.log("处理图片数据失败: ", res);
                }
              });
            });
          }
        });
      },
      fail: function (res) {
        console.log("用户选择文件失败...");
      }
    });
  },

  userPrintImage(event) {
    const that = this;

    if (that.data.selectedImageFilePath == null) {
      wx.showToast({
        title: "未选择图片",
        icon: "error",
      });
      return;
    }

    if (that.data.finalImageData == null) {
      wx.showToast({
        title: "图片处理中, 请稍后重试...",
        icon: "loading",
      });
      return;
    }

    const imageData = that.data.finalImageData;

    var index = 0;
    const nRows = 384;
    // 开始打印
    const printTimerId = setInterval(() => {
      if (index >= nRows) {
        clearInterval(printTimerId);
        return;
      }

      const deviceId = app.globalData.connectedDevice.deviceId;

      // 发送开始包
      const startPkt = new Uint8ClampedArray([0x4F, 0x57, 0x4C, 0x01]);
      wx.writeBLECharacteristicValue({
        characteristicId: OWL_BLE_DATA_INPUT_UUID,
        deviceId: deviceId,
        serviceId: OWL_BLE_SERVICE_UUID,
        value: startPkt.buffer,
        success: res => {
          console.log("send startPkt ok");
        },
        fail: res => {
          console.log(res);
        }
      });

      // 发送数据包
      var batch = 100;
      for (let i = index; i < index + batch && i < 384; i++) {
        const dataPkt = new Uint8ClampedArray(52);
        dataPkt[0] = 0x4F;
        dataPkt[1] = 0x57;
        dataPkt[2] = 0x4C;
        dataPkt[3] = 0x02;
        for (let j = 0; j < 48; j++) {
          dataPkt[j + 4] = imageData[i * 48 + j];
        }

        wx.writeBLECharacteristicValue({
          characteristicId: OWL_BLE_DATA_INPUT_UUID,
          deviceId: deviceId,
          serviceId: OWL_BLE_SERVICE_UUID,
          value: dataPkt.buffer,
          success: res => {
            console.log(i + ": send dataPkt ok");
          },
          fail: res => {
            console.log(res);
          }
        });
      }

      index += batch;

      // 发送结束包
      const endPkt = new Uint8ClampedArray([0x4F, 0x57, 0x4C, 0xFF]);
      wx.writeBLECharacteristicValue({
        characteristicId: OWL_BLE_DATA_INPUT_UUID,
        deviceId: deviceId,
        serviceId: OWL_BLE_SERVICE_UUID,
        value: endPkt.buffer,
        success: res => {
          console.log("send endPkt ok");
        },
        fail: res => {
          console.log(res);
        }
      });

    }, 40);
  }
});