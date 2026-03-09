# 贡献约定

当前项目仍处于早期阶段，但从一开始就按长期维护的标准执行。

## 基本原则

- 默认使用中文编写文档、注释与提交说明
- 优先保持模块边界清晰，而不是过早追求抽象层数量
- 先验证 Windows 原生行为，再讨论跨平台可能性
- 任何会影响产品范围或架构边界的改动，都应先更新文档

## 代码约定

- 遵循仓库中的 `.editorconfig` 与 `.clang-format`
- 新增业务逻辑前，先判断其应落入 `core`、平台层还是功能层
- 不把 Win32 细节泄漏到本应平台无关的模块
- 注释只写必要的信息，重点解释约束、原因和边界

## 构建约定

- 默认使用 `MSVC` 构建与调试
- 使用 `clang-cl` 做补充校验
- 在 WSL 中编辑是允许的，但 Windows 才是实际构建环境

## 文档约定

- 产品边界写入 [docs/product.md](/home/gwf/Projects/captureZY/docs/product.md)
- 架构边界写入 [docs/architecture.md](/home/gwf/Projects/captureZY/docs/architecture.md)
- 已定技术决策写入 [docs/decisions.md](/home/gwf/Projects/captureZY/docs/decisions.md)
- 版本路线写入 [docs/roadmap.md](/home/gwf/Projects/captureZY/docs/roadmap.md)
