# Performance Tuning

> StreamHub fork：本机采集、编码、显示管理与虚拟输入已移除；下文涉及这些功能的上游指南仅供历史参考。当前构建、依赖和验证入口见 [完整裁剪记录](streamhub/slimming-core.md)。
In addition to the options available in the [Configuration](configuration.md) section, there are a few additional
system options that can be used to help improve the performance of Sunshine.

## AMD

In Windows, enabling *Enhanced Sync* in AMD's settings may help reduce the latency by an additional frame. This
applies to `amfenc` and `libx264`.

## NVIDIA

Enabling *Fast Sync* in Nvidia settings may help reduce latency.

<div class="section_buttons">

| Previous            |          Next |
|:--------------------|--------------:|
| [Guides](guides.md) | [API](api.md) |

</div>

<details style="display: none;">
  <summary></summary>
  [TOC]
</details>
