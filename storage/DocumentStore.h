#pragma once

#include <QList>
#include <QString>

class Database;

/**
 * 参考资料模式文档元数据（schema v3 的 documents 表）。
 *
 * 注意：本表只存**元数据**（token 估算、章节来源类型、章节数），
 * **不存全文**。全文注入时按 chunks 表的 ordinal 顺序拼接即可。
 * 这样旧文件（索引于 v2 时）在本表无行 → 自动回退到现有片段模式，
 * 用户不需要重建索引也能正常使用。
 */
struct DocumentRecord
{
    qint64 id = 0;
    qint64 fileId = 0;
    int tokenEstTotal = 0;
    QString sectionSource;   // heading / outline / sheet / slide / page_approx / none
    int sectionCount = 0;
    qint64 builtAt = 0;
};

struct SectionRecord
{
    qint64 id = 0;
    qint64 documentId = 0;
    int ordinal = 0;       // 文档内章节顺序（从 0 开始）
    QString title;         // Heading 文本 / 工作表名 / 页码 / 块起始文本
    int chunkStart = 0;    // 起始 chunk ordinal（文件内）
    int chunkEnd = 0;      // 结束 chunk ordinal（含）
    int tokenEst = 0;
};

/**
 * 参考资料模式存储层（schema v3）。
 *
 * 职责：documents / sections 两表的纯 CRUD。
 * 不含业务判断；失败路径返回 false + error，绝不抛异常。
 * 风格与 LibraryStore 完全一致。
 */
class DocumentStore
{
public:
    explicit DocumentStore(Database *database);

    /// 底层数据库是否可用
    bool isAvailable() const;

    // ---------------------------------------------------------- documents
    /// 某文件是否已有 documents 行
    bool hasDocument(qint64 fileId, QString *error) const;
    /// 按 fileId 查（零或一行，file_id 有 UNIQUE 约束）
    bool documentByFile(qint64 fileId, DocumentRecord *out, QString *error) const;
    bool documentById(qint64 id, DocumentRecord *out, QString *error) const;
    /// 插入或替换：已有同 fileId 行则整体替换（含级联删除 sections）
    bool upsertDocument(const DocumentRecord &record, qint64 *newId, QString *error) const;
    /// 删除 documents 行（级联删除 sections）
    bool removeDocumentByFile(qint64 fileId, QString *error) const;

    // ---------------------------------------------------------- sections
    /// 取某文档全部章节，按 ordinal 升序
    QList<SectionRecord> sectionsOfDocument(qint64 documentId, QString *error) const;
    /// 批量替换某文档的章节（事务：清旧 → 插新）
    bool replaceSections(qint64 documentId, const QList<SectionRecord> &sections,
                         QString *error) const;

private:
    Database *m_database = nullptr;
};
