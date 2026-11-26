# 素材与组件

## 原作资料

蒸気機由卜卜口（itorr）制作。本项目使用独立的 C++ 实现。原作 19 组预设的名称与数值见 assets/factory.json，2026-11-26 的 94 条公共预设快照见 assets/community.json。assets/gallery.json 保存网站列出的 76 个样片文件名，客户端直接向原站请求图片。原作水印保存在 assets/watermark.png，用于复现水印选项。原站样片的权利归各自权利人。

- [蒸気機](https://magiconch.com/vaporwave/)
- [原作代码](https://github.com/itorr/vaporwave)
- [公共预设接口](https://magiconch.com/api/vaporwave/style)
- [原作水印](https://magiconch.com/vaporwave/logo.png)

电子包浆参考卜卜口的网页版及其逐轮绿化、转发水印和波普布局。桌面程序使用独立的 C++ 渲染与 GIF 编码实现，并提供可编辑的桌面版用户名示例。测试中的 web-patina-12.jpg 由原版 Chrome 渲染流程生成，使用同目录的 input.png、12 轮、60% 画质、全尺寸，关闭水印和随机偏移，固定随机值为零。patina-motion.gif 是独立生成的三帧测试图。[电子包浆](https://magiconch.com/patina/) · [原作仓库](https://github.com/itorr/patina)

## 内置样片

以下三张样片由内置 ImageGen 生成，转换为 JPEG 随桌面端离线提供。应用内信号色条由 C++ 绘制。tests/reference 下的输入和网页输出使用海岸样片制作。

**assets/coast.jpg · 海岸列车**

> Use case: photorealistic-natural. A full-bleed still photograph for the default sample in a polished analog image editing app. Wide 16:9 horizontal cinema frame. Quiet 1990s Japanese seaside railroad at blue hour, an old boxy two-car coral red and cream commuter train, seen close in three-quarter side profile on the right half of the frame traveling left, the lit front just right of center and long windows going to the right edge, pale teal ocean and horizon visible on the left, layered overhead power cables and a crossing signal in silhouette on the left. Periwinkle blue twilight fading to peach pink at the horizon. Atmospheric, large clean shapes, beautiful warm highlights on brushed painted metal, tranquil, photographic, editorial film still on 35mm color negative, natural restrained contrast and natural colors with detailed dark areas. Eye-level from the roadside, no people in focus, no lettering overlays or UI, no border, no heavy effects or scanlines. One coherent photographic exposure, not a collage.

**assets/night.jpg · 午夜街角**

> Use case: photorealistic-natural. A single wide 16:9 editorial analog photograph for an image editor sample, a softly illuminated mint-green vending machine on the left under a storefront awning on a deserted Japanese side street late at night, narrow lane vanishing to the right, dark indigo shadows, soft amber window light and restrained bottle label colors. Distinct moody lighting, fine metal surfaces and pavement reflecting soft light. 1990s everyday documentary photography, no people, no overlay or borders, full-bleed single frame.

**assets/pool.jpg · 夏日泳池**

> Use case: photorealistic-natural. A single wide 16:9 editorial image for a photo editor sample. A sunlit modernist outdoor swimming pool photographed at midday in the late 1980s, pale peach angular concrete walls and deep clear turquoise water with bright caustics. Strong geometric diagonal sunlight and hard shadows from architectural forms, small chrome pool ladder, fine detail on tiles, simple serene composition seen from a slight elevation. Color negative photography, dry warm summer light, one full-bleed photographic frame, no people, no lettering or borders.

## 字体与运行组件

字标采用 Barlow Condensed SemiBold，附 SIL Open Font License。中文界面使用系统字体。[Barlow Condensed](https://github.com/google/fonts/tree/main/ofl/barlowcondensed)

Windows 发行包动态链接 Qt 6.8.3。Qt、其中的第三方组件及 MSVC 运行库信息见 licenses 目录。

