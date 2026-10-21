#pragma once

#include "core/library/extract/Extractor.h"

#include <QString>
#include <QStringList>

#include <memory>
#include <vector>

/**
 * 解析器注册表。
 *
 * 上层（M4 索引器 / M6 对话）只调用 `forPath()` 取解析器，**不写格式分支**；
 * 新增格式＝新增一个 `Extractor` 实现并在构造函数里登记，核心业务代码零改动。
 */
class ExtractorRegistry
{
public:
    /// 登记本次范围内的解析器（纯文本 / CSV / JSON；视频为预留注册点）
    ExtractorRegistry();

    /// 登记自定义解析器（测试注入 / 后续格式）。**同 id 覆盖先注册者**，
    /// 注册表独占所有权（`std::unique_ptr`）
    void add(std::unique_ptr<Extractor> extractor);

    /// 负责该文件的解析器；无匹配返回 nullptr（调用方需给出明确提示）
    Extractor *forPath(const QString &path) const;

    /// 已登记的解析器 id（注册顺序，同 id 只留最新）
    QStringList ids() const;

    /// 便捷入口：无匹配解析器时返回 false，且 `error` 说明「暂不支持该格式」
    bool extract(const QString &path, ExtractionResult *result, QString *error) const;

private:
    std::vector<std::unique_ptr<Extractor>> m_extractors;
};
