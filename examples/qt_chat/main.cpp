#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>
#include <chrono>
#include <cstdint>
#include <exception>
#include <memory>
#include <sioxx/sioxx.hpp>
#include <string>
#include <utility>
#include <vector>

namespace
{

QString string_field(const sioxx::json& value, const char* key,
                     const QString& fallback = {})
{
  if (!value.is_object()) return fallback;
  const auto field = value.find(key);
  if (field == value.end() || !field->is_string()) return fallback;
  return QString::fromStdString(field->get<std::string>());
}

const sioxx::json* first_argument(const sioxx::message& arguments)
{
  if (!arguments.is_array() || arguments.empty()) return nullptr;
  return &arguments.front();
}

class chat_window final : public QWidget
{
 public:
  chat_window()
  {
    setWindowTitle("sioxx Qt chat");
    resize(720, 520);

    url_->setText("ws://localhost:3000");
    namespace_->setText("/chat");
    name_->setText("Qt user");
    parser_->addItems({"JSON", "MessagePack"});
    transcript_->setReadOnly(true);
    message_->setPlaceholderText("Type a message...");
    status_->setText("Disconnected");

    auto* connection_form = new QFormLayout;
    connection_form->addRow("Server URL", url_);
    connection_form->addRow("Namespace", namespace_);
    connection_form->addRow("Display name", name_);

    auto* options = new QHBoxLayout;
    options->addWidget(new QLabel("Parser"));
    options->addWidget(parser_);
    options->addWidget(polling_);
    options->addStretch();
    options->addWidget(connect_);

    auto* composer = new QHBoxLayout;
    composer->addWidget(message_, 1);
    composer->addWidget(file_);
    composer->addWidget(send_);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(connection_form);
    layout->addLayout(options);
    layout->addWidget(status_);
    layout->addWidget(transcript_, 1);
    layout->addLayout(composer);

    polling_->setText("Force HTTP polling");
    connect_->setText("Connect");
    file_->setText("Send file...");
    send_->setText("Send");
    set_connected(false);

    connect(connect_, &QPushButton::clicked, this,
            [this] { toggle_connection(); });
    connect(send_, &QPushButton::clicked, this, [this] { send_message(); });
    connect(message_, &QLineEdit::returnPressed, this,
            [this] { send_message(); });
    connect(file_, &QPushButton::clicked, this, [this] { send_file(); });
    connect(parser_, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { update_file_button(); });
  }

  ~chat_window() override
  {
    if (client_) client_->close();
    socket_.reset();
    client_.reset();
  }

 private:
  void toggle_connection()
  {
    if (client_)
    {
      append_system("Disconnecting");
      client_->close();
      socket_.reset();
      client_.reset();
      set_connected(false);
      return;
    }

    if (url_->text().trimmed().isEmpty() ||
        namespace_->text().trimmed().isEmpty() ||
        name_->text().trimmed().isEmpty())
    {
      append_system("Server URL, namespace, and display name are required");
      return;
    }

    sioxx::client_options options;
    options.parser = parser_->currentIndex() == 0 ? sioxx::parser_kind::json
                                                  : sioxx::parser_kind::msgpack;
    options.force_http_polling = polling_->isChecked();
    options.reconnect_attempts = 5;
    options.reconnect_delay = std::chrono::milliseconds(1000);
    options.reconnect_delay_max = std::chrono::milliseconds(10000);

    try
    {
      client_ = std::make_unique<sioxx::client>(options);
      client_->socket("/");  // Makes the client-level open listener observable.
      socket_ = client_->socket(
        namespace_->text().trimmed().toStdString(),
        sioxx::json{{"username", name_->text().trimmed().toStdString()}});
      install_sioxx_handlers();

      const auto server_url = url_->text().trimmed();
      set_connecting(true);
      append_system("Connecting to " + server_url);
      client_->connect(server_url.toStdString());
    }
    catch (const std::exception& error)
    {
      append_system("Connection setup failed: " +
                    QString::fromUtf8(error.what()));
      socket_.reset();
      client_.reset();
      set_connected(false);
    }
  }

  void install_sioxx_handlers()
  {
    client_->set_open_listener(
      [this] { post([this] { append_system("Engine.IO connection open"); }); });
    client_->set_close_listener(
      [this](const std::string& reason)
      {
        post(
          [this, reason]
          {
            append_system("Connection closed: " +
                          QString::fromStdString(reason));
            set_connected(false);
          });
      });
    client_->set_error_listener(
      [this](const std::string& error)
      {
        post([this, error]
             { append_system("sioxx: " + QString::fromStdString(error)); });
      });
    client_->set_fail_listener(
      [this]
      {
        post(
          [this]
          {
            append_system("Reconnect attempts exhausted");
            set_connected(false);
          });
      });

    socket_->on_connect(
      [this]
      {
        post(
          [this]
          {
            append_system("Chat namespace connected");
            set_connected(true);
          });
      });
    socket_->on_disconnect(
      [this](const std::string& reason)
      {
        post(
          [this, reason]
          {
            append_system("Namespace disconnected: " +
                          QString::fromStdString(reason));
            set_connected(false);
          });
      });
    socket_->on("chat_message",
                [this](const std::string&, sioxx::message arguments)
                {
                  post(
                    [this, arguments = std::move(arguments)]
                    {
                      const auto* value = first_argument(arguments);
                      if (!value) return;
                      append_chat(
                        string_field(*value, "username", "Unknown"),
                        string_field(*value, "text", "<invalid message>"));
                    });
                });
    socket_->on(
      "server_notice",
      [this](const std::string&, sioxx::message arguments)
      {
        post(
          [this, arguments = std::move(arguments)]
          {
            const auto* value = first_argument(arguments);
            if (value && value->is_string())
              append_system(QString::fromStdString(value->get<std::string>()));
          });
      });
    socket_->on("welcome",
                [this](const std::string&, sioxx::message arguments,
                       sioxx::socket::ack_callback acknowledge)
                {
                  if (acknowledge)
                    acknowledge(sioxx::json::array(
                      {{{"ready", true}, {"client", "sioxx Qt example"}}}));
                  post(
                    [this, arguments = std::move(arguments)]
                    {
                      const auto* value = first_argument(arguments);
                      append_system(value
                                      ? string_field(*value, "text", "Welcome")
                                      : "Welcome");
                    });
                });
    socket_->on(
      "attachment",
      [this](const std::string&, sioxx::message arguments)
      {
        post(
          [this, arguments = std::move(arguments)]
          {
            const auto* value = first_argument(arguments);
            if (!value || !value->is_object()) return;
            const auto bytes = value->find("bytes");
            const auto size = bytes != value->end() && bytes->is_binary()
                                ? bytes->get_binary().size()
                                : 0;
            append_system(string_field(*value, "username", "Unknown") +
                          " shared " + string_field(*value, "name", "a file") +
                          " (" + QString::number(size) + " bytes)");
          });
      });
    socket_->on("connect_error",
                [this](const std::string&, sioxx::message arguments)
                {
                  post(
                    [this, arguments = std::move(arguments)]
                    {
                      append_system("Namespace rejected: " +
                                    QString::fromStdString(arguments.dump()));
                      set_connected(false);
                    });
                });
    socket_->on_any(
      [this](const std::string& event, sioxx::message arguments)
      {
        if (event == "chat_message" || event == "server_notice" ||
            event == "welcome" || event == "attachment" ||
            event == "connect_error")
          return;
        post(
          [this, event, arguments = std::move(arguments)]
          {
            append_system("Unhandled event '" + QString::fromStdString(event) +
                          "': " + QString::fromStdString(arguments.dump()));
          });
      });
  }

  template <typename Function> void post(Function&& function)
  {
    // sioxx callbacks run on its networking thread. A queued invocation keeps
    // every QWidget access on Qt's GUI thread.
    QMetaObject::invokeMethod(this, std::forward<Function>(function),
                              Qt::QueuedConnection);
  }

  void send_message()
  {
    const auto text = message_->text().trimmed();
    if (!socket_ || !socket_->connected() || text.isEmpty()) return;

    socket_->emit(
      "chat_message", sioxx::json{{"text", text.toStdString()}},
      [this](sioxx::message reply)
      {
        post(
          [this, reply = std::move(reply)]
          {
            const auto* value = first_argument(reply);
            const auto status = value ? string_field(*value, "status") : "";
            if (status == "delivered")
              append_system("Message delivered (server acknowledgement)");
            else
              append_system("Message acknowledgement: " +
                            QString::fromStdString(reply.dump()));
          });
      });
    message_->clear();
  }

  void send_file()
  {
    if (!socket_ || !socket_->connected() || parser_->currentIndex() == 0)
      return;

    const auto path = QFileDialog::getOpenFileName(this, "Share a small file");
    if (path.isEmpty()) return;

    QFile input(path);
    constexpr qint64 max_size = 1024 * 1024;
    if (!input.open(QIODevice::ReadOnly) || input.size() > max_size)
    {
      append_system("Choose a readable file no larger than 1 MiB");
      return;
    }

    const auto contents = input.readAll();
    std::vector<std::uint8_t> bytes(contents.begin(), contents.end());
    socket_->emit(
      "attachment",
      sioxx::json{{"name", QFileInfo(path).fileName().toStdString()},
                  {"bytes", sioxx::binary_message(std::move(bytes))}},
      [this](sioxx::message reply)
      {
        post(
          [this, reply = std::move(reply)]
          {
            append_system("Attachment acknowledgement: " +
                          QString::fromStdString(reply.dump()));
          });
      });
  }

  void set_connecting(bool connecting)
  {
    status_->setText(connecting ? "Connecting..." : "Disconnected");
    connect_->setText(connecting ? "Disconnect" : "Connect");
    connect_->setEnabled(true);
    url_->setEnabled(!connecting);
    namespace_->setEnabled(!connecting);
    name_->setEnabled(!connecting);
    parser_->setEnabled(!connecting);
    polling_->setEnabled(!connecting);
    send_->setEnabled(false);
    update_file_button();
  }

  void set_connected(bool connected)
  {
    status_->setText(connected ? "Connected" : "Disconnected");
    connect_->setText(connected || client_ ? "Disconnect" : "Connect");
    url_->setEnabled(!client_);
    namespace_->setEnabled(!client_);
    name_->setEnabled(!client_);
    parser_->setEnabled(!client_);
    polling_->setEnabled(!client_);
    message_->setEnabled(connected);
    send_->setEnabled(connected);
    update_file_button();
  }

  void update_file_button()
  {
    file_->setEnabled(socket_ && socket_->connected() &&
                      parser_->currentIndex() == 1);
    file_->setToolTip(parser_->currentIndex() == 0
                        ? "Binary attachments require MessagePack"
                        : "Share a binary payload (maximum 1 MiB)");
  }

  void append_system(const QString& text)
  {
    transcript_->appendPlainText(
      "[" + QDateTime::currentDateTime().toString("HH:mm:ss") + "] " + text);
  }

  void append_chat(const QString& author, const QString& text)
  {
    transcript_->appendPlainText(
      "[" + QDateTime::currentDateTime().toString("HH:mm:ss") + "] " + author +
      ": " + text);
  }

  QLineEdit* url_{new QLineEdit};
  QLineEdit* namespace_{new QLineEdit};
  QLineEdit* name_{new QLineEdit};
  QComboBox* parser_{new QComboBox};
  QCheckBox* polling_{new QCheckBox};
  QPushButton* connect_{new QPushButton};
  QLabel* status_{new QLabel};
  QPlainTextEdit* transcript_{new QPlainTextEdit};
  QLineEdit* message_{new QLineEdit};
  QPushButton* file_{new QPushButton};
  QPushButton* send_{new QPushButton};

  std::unique_ptr<sioxx::client> client_;
  std::shared_ptr<sioxx::socket> socket_;
};

}  // namespace

int main(int argc, char* argv[])
{
  QApplication application(argc, argv);
  chat_window window;
  window.show();
  return application.exec();
}
