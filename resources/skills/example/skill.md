# 示例技能

本文件是内置 Skill 的文档正文占位。

- 目录规范：`resources/skills/<id>/`，必须包含 `manifest.json` 与 manifest 中 `entry` 指向的文档。
- 加载时机：应用启动时由 `core/skill/SkillRegistry` 扫描装载，放入即生效，无需改代码。
- 阶段一说明：只完成扫描与读取，Skill 的实际调用逻辑在阶段三实现。
