这是一个通过3.5接口实时生成与解析timecode的库，
基础的逻辑是https://github.com/x42/libltc
我只是在上面加上了实时的解析与生成的功能

startRecording @ 1: 启动录制功能。

stopRecording @ 2: 停止录制功能。


setCallback @ 3: 设置回调函数，用于处理录制事件。

Myltc @ 4: 自定义的 LTC 处理方法。

GetCurrentTimecode @ 5: 获取当前时间码。

LTC_Init @ 6: 初始化 LTC 系统。

LTC_Start @ 7: 启动 LTC 功能。

LTC_Stop @ 8: 停止 LTC 功能。

LTC_SetStopTime @ 9: 设置停止时间。

LTC_GetCurrentTime @ 10: 获取当前时间。

LTC_Cleanup @ 11: 清理 LTC 系统。
