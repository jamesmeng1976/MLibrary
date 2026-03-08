#include <QApplication>
#include <QKeyEvent>
#include <QPushButton>
#include <QGridLayout>
#include <QLineEdit>
#include <QTextEdit>
#include <QDoubleSpinBox>

class VirtualKeypad : public QWidget {
    Q_OBJECT
public:
    VirtualKeypad(QWidget *parent = nullptr) : QWidget(parent) {
        // 设置窗口标志：无边框、置顶、且不抢占主界面的活跃状态
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
        // 关键：键盘本身不能获取焦点
        setFocusPolicy(Qt::NoFocus); 
        
        QGridLayout *layout = new QGridLayout(this);
        layout->setSpacing(5);
        layout->setContentsMargins(10, 10, 10, 10);

        // 定义按键布局 (4行4列)
        struct KeyDef { int row, col, spanRow, spanCol; QString text; int key; };
        QList<KeyDef> keys = {
            {0, 0, 1, 1, "7", Qt::Key_7}, {0, 1, 1, 1, "8", Qt::Key_8}, {0, 2, 1, 1, "9", Qt::Key_9}, {0, 3, 1, 1, "AC", 0},
            {1, 0, 1, 1, "4", Qt::Key_4}, {1, 1, 1, 1, "5", Qt::Key_5}, {1, 2, 1, 1, "6", Qt::Key_6}, {1, 3, 1, 1, "<-", Qt::Key_Backspace},
            {2, 0, 1, 1, "1", Qt::Key_1}, {2, 1, 1, 1, "2", Qt::Key_2}, {2, 2, 1, 1, "3", Qt::Key_3}, {2, 3, 1, 1, "Space", Qt::Key_Space},
            {3, 0, 1, 2, "0", Qt::Key_0}, {3, 2, 1, 1, ".", Qt::Key_Period}, {3, 3, 1, 1, "Hide", 0} 
            // 注意：0键跨两列 (spanCol = 2)
        };

        for (const auto &k : keys) {
            QPushButton *btn = new QPushButton(k.text, this);
            btn->setFocusPolicy(Qt::NoFocus); // 关键：所有按钮都不能获取焦点
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            
            // 为特殊按键设置 ObjectName，方便 QSS 独立控制样式
            if (k.text == "AC") btn->setObjectName("btnAC");
            else if (k.text == "Hide") btn->setObjectName("btnHide");
            else if (k.text == "<-") btn->setObjectName("btnBackspace");

            layout->addWidget(btn, k.row, k.col, k.spanRow, k.spanCol);

            // 事件绑定
            connect(btn, &QPushButton::clicked, this, [=]() {
                if (k.text == "AC") {
                    handleAllClear();
                } else if (k.text == "Hide") {
                    this->hide();
                } else {
                    QString sendText = (k.text == "<-" || k.text == "Space") ? "" : k.text;
                    if (k.text == "Space") sendText = " "; 
                    sendKeyEvent(k.key, sendText);
                }
            });
        }
        
        // 设置键盘初始大小
        setFixedSize(320, 280);
    }

private:
    // 发送按键事件
    void sendKeyEvent(int key, const QString &text) {
        QWidget *receiver = QApplication::focusWidget();
        if (!receiver) return;

        QKeyEvent keyPress(QEvent::KeyPress, key, Qt::NoModifier, text);
        QApplication::sendEvent(receiver, &keyPress);

        QKeyEvent keyRelease(QEvent::KeyRelease, key, Qt::NoModifier, text);
        QApplication::sendEvent(receiver, &keyRelease);
    }

    // 处理 AC (All Clear) 逻辑
    void handleAllClear() {
        QWidget *receiver = QApplication::focusWidget();
        if (!receiver) return;

        // 根据实际使用的控件类型进行清空
        if (QLineEdit *le = qobject_cast<QLineEdit*>(receiver)) {
            le->clear();
        } else if (QDoubleSpinBox *dsb = qobject_cast<QDoubleSpinBox*>(receiver)) {
            dsb->clear();
        } else if (QTextEdit *te = qobject_cast<QTextEdit*>(receiver)) {
            te->clear();
        }
    }
};