#pragma once

#include <QString>

class LibraryStore;

/**
 * 全文拼接器：按 ordinal 顺序拼接片段并去除相邻重叠。
 *
 * 片段切分时有 overlap（默认 120 字符），相邻片段末尾与开头会重复。
 * 不去重会导致注入内容重复，既浪费预算又降低可读性。
 *
 * 去重算法：对相邻两块，从 overlap 长度递减尝试，找到「前块尾 == 后块头」的最大长度并跳过。
 */
class FullTextAssembler
{
public:
    explicit FullTextAssembler(LibraryStore *store);

    /// 拼接某文件全部片段；返回空串表示无片段或出错
    QString assemble(qint64 fileId, int overlap, int *outTokenEst, QString *error) const;

    /// 拼接 [ordinalFrom, ordinalTo] 闭区间内的片段（章节级复用）
    QString assembleRange(qint64 fileId, int ordinalFrom, int ordinalTo, int overlap,
                          int *outTokenEst, QString *error) const;

private:
    LibraryStore *m_store = nullptr;
};
