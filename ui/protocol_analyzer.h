#ifndef PROTOCOL_ANALYZER_H
#define PROTOCOL_ANALYZER_H

#include <QDialog>
#include <QPlainTextEdit>
#include <QTextEdit>

class ProtocolAnalyzerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProtocolAnalyzerDialog(QWidget* parent = nullptr);

private slots:
    void parse();
    void clear();

private:
    QString parse_transport_layer(const QByteArray& data);
    QString parse_inner_frame(const QByteArray& data);

    QPlainTextEdit* m_text_input = nullptr;
    QTextEdit* m_output = nullptr;
};

#endif // PROTOCOL_ANALYZER_H
