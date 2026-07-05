#include "RCHD24.h"

#include <QWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QInputDialog>
#include <QDialog>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QIntValidator>
#include <QMessageBox>

// ---------------------------------------------------------------------------
// Construccion
// ---------------------------------------------------------------------------
RCHD24::RCHD24(QWidget *parent)
    : QMainWindow(parent)
    , m_tcp(new Tcp(this))
    , m_bt(new Bluetooth(this))
{
    // Conectamos las senales de AMBOS transportes a los mismos manejadores.
    // Solo uno estara conectado a la vez; los eventos del otro no llegan.
    connect(m_tcp, &Tcp::connected,       this, [this]{ onConnected(QStringLiteral("WiFi/TCP")); });
    connect(m_tcp, &Tcp::disconnected,    this, &RCHD24::onDisconnected);
    connect(m_tcp, &Tcp::lineReceived,    this, &RCHD24::onLineReceived);
    connect(m_tcp, &Tcp::errorOccurred,   this, &RCHD24::onTransportError);

    connect(m_bt, &Bluetooth::connected,     this, [this]{ onConnected(QStringLiteral("Bluetooth")); });
    connect(m_bt, &Bluetooth::disconnected,  this, &RCHD24::onDisconnected);
    connect(m_bt, &Bluetooth::lineReceived,  this, &RCHD24::onLineReceived);
    connect(m_bt, &Bluetooth::errorOccurred, this, &RCHD24::onTransportError);
}

bool RCHD24::create(const std::string &title)
{
    setWindowTitle(title.c_str());
    setWindowFlag(Qt::WindowFullscreenButtonHint, false);

    // Panel central: header (conexion), ARM, transporte y LOCATE.
    auto *central = new QWidget(this);
    auto *layout  = new QVBoxLayout(central);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(10);

    // El header (boton Conexion) queda SIEMPRE habilitado. Los paneles
    // operativos van en un contenedor aparte que se deshabilita si no hay
    // conexion (asi no se puede tocar transporte/ARM sin equipo, pero SI
    // se puede abrir el menu para conectar).
    m_controls = new QWidget(central);
    auto *controlsLayout = new QVBoxLayout(m_controls);
    controlsLayout->setContentsMargins(0, 0, 0, 0);
    controlsLayout->setSpacing(10);
    controlsLayout->addWidget(buildArmPanel(), /*stretch*/ 1);
    controlsLayout->addWidget(buildTransportPanel());
    controlsLayout->addWidget(buildLocatePanel());

    layout->addWidget(buildHeader());
    layout->addWidget(m_controls, /*stretch*/ 1);

    setCentralWidget(central);

    // Barra de estado con el estado de conexion + ultima respuesta del equipo.
    m_statusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(m_statusLabel);
    setConnected(false);

    return true;
}

// ---------------------------------------------------------------------------
// Header: boton "Conexion" (siempre visible) + estado
// ---------------------------------------------------------------------------
QWidget *RCHD24::buildHeader()
{
    auto *header = new QWidget(this);
    auto *row    = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);

    // Boton con menu desplegable: funciona igual en desktop y en Android
    // (a diferencia de la barra de menu, que en Android no se muestra y en
    // macOS se va a la barra global del sistema).
    auto *connBtn = new QPushButton(tr("☰  Conexion"), header);
    connBtn->setMinimumHeight(36);
    connBtn->setMenu(buildConnectionMenu());

    row->addWidget(connBtn);
    row->addStretch(1);

    return header;
}

// ---------------------------------------------------------------------------
// Menu desplegable de conexion
// ---------------------------------------------------------------------------
QMenu *RCHD24::buildConnectionMenu()
{
    auto *conn = new QMenu(tr("Conexion"), this);

    conn->addAction(tr("Conectar por WiFi/TCP..."),  this, &RCHD24::connectTcpDialog);
    conn->addAction(tr("Conectar por Bluetooth..."), this, &RCHD24::connectBluetoothDialog);
    conn->addSeparator();
    conn->addAction(tr("Desconectar"),               this, &RCHD24::disconnectActive);

    return conn;
}

void RCHD24::connectTcpDialog()
{
    bool ok = false;
    // Por defecto el nombre mDNS que anuncia el firmware. Se puede poner una IP.
    const QString host = QInputDialog::getText(
        this, tr("Conectar por WiFi/TCP"), tr("Host o IP del HD24:"),
        QLineEdit::Normal, QStringLiteral("hd24.local"), &ok);
    if (!ok || host.isEmpty())
        return;

    const int port = QInputDialog::getInt(
        this, tr("Conectar por WiFi/TCP"), tr("Puerto:"),
        Tcp::DEFAULT_PORT, 1, 65535, 1, &ok);
    if (!ok)
        return;

    m_active = Active::Tcp;
    m_bt->disconnectFromDevice();          // por las dudas, cerramos el otro
    statusBar()->showMessage(tr("Conectando a %1:%2...").arg(host).arg(port));
    m_tcp->connectToHost(host, static_cast<quint16>(port));
}

void RCHD24::connectBluetoothDialog()
{
    // Dialogo simple: busca equipos y deja elegir el HD24 de la lista.
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Conectar por Bluetooth"));
    auto *v = new QVBoxLayout(&dlg);

    auto *list = new QListWidget(&dlg);
    auto *scan = new QPushButton(tr("Buscar dispositivos"), &dlg);
    auto *box  = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    box->button(QDialogButtonBox::Ok)->setText(tr("Conectar"));

    v->addWidget(scan);
    v->addWidget(list, 1);
    v->addWidget(box);

    // Cada equipo hallado se agrega a la lista guardando su MAC en el item.
    auto connDiscovered = connect(m_bt, &Bluetooth::deviceDiscovered, &dlg,
        [list](const QString &name, const QString &address)
        {
            const QString label = name.isEmpty() ? address
                                                 : QStringLiteral("%1  (%2)").arg(name, address);
            auto *item = new QListWidgetItem(label, list);
            item->setData(Qt::UserRole, address);
        });
    auto connFinished = connect(m_bt, &Bluetooth::discoveryFinished, scan,
        [scan]{ scan->setEnabled(true); scan->setText(QObject::tr("Buscar dispositivos")); });

    connect(scan, &QPushButton::clicked, &dlg, [this, scan, list]
    {
        list->clear();
        scan->setEnabled(false);
        scan->setText(tr("Buscando..."));
        m_bt->startDiscovery();
    });
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Empezamos a buscar apenas se abre el dialogo.
    list->clear();
    scan->setEnabled(false);
    scan->setText(tr("Buscando..."));
    m_bt->startDiscovery();

    const int result = dlg.exec();

    // Salimos del dialogo: cortamos la busqueda y desconectamos sus senales.
    m_bt->stopDiscovery();
    disconnect(connDiscovered);
    disconnect(connFinished);

    if (result != QDialog::Accepted || !list->currentItem())
        return;

    const QString address = list->currentItem()->data(Qt::UserRole).toString();
    m_active = Active::Bluetooth;
    m_tcp->disconnectFromHost();
    statusBar()->showMessage(tr("Conectando a %1...").arg(address));
    m_bt->connectToDevice(address);
}

void RCHD24::disconnectActive()
{
    m_tcp->disconnectFromHost();
    m_bt->disconnectFromDevice();
    m_active = Active::None;
    setConnected(false);
}

// ---------------------------------------------------------------------------
// Panel ARM: 24 botones chicos, alternables con el dedo.
// ---------------------------------------------------------------------------
QWidget *RCHD24::buildArmPanel()
{
    auto *group = new QGroupBox(tr("Pistas (ARM)"), this);
    auto *grid  = new QGridLayout(group);
    grid->setSpacing(4);

    // Estilo touch: boton chico pero comodo; en rojo cuando esta armado.
    const QString style = QStringLiteral(
        "QPushButton { font-weight: bold; border: 1px solid #888; border-radius: 6px;"
        "  background: #eee; }"
        "QPushButton:checked { background: #c0392b; color: white; border-color: #7f1d1d; }");

    const int COLS = 6;   // 6 x 4 = 24
    m_armButtons.clear();
    m_armButtons.reserve(NUM_TRACKS);

    for (int t = 1; t <= NUM_TRACKS; ++t)
    {
        auto *btn = new QPushButton(QString::number(t), group);
        btn->setCheckable(true);
        btn->setMinimumSize(44, 44);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        btn->setStyleSheet(style);

        connect(btn, &QPushButton::toggled, this, [this, t](bool on){ onArmToggled(t, on); });

        const int row = (t - 1) / COLS;
        const int col = (t - 1) % COLS;
        grid->addWidget(btn, row, col);
        m_armButtons.append(btn);
    }

    return group;
}

// ---------------------------------------------------------------------------
// Panel de transporte: REW / PLAY / PAUSE / STOP / FF / REC.
// ---------------------------------------------------------------------------
QWidget *RCHD24::buildTransportPanel()
{
    auto *group = new QGroupBox(tr("Transporte"), this);
    auto *row   = new QHBoxLayout(group);
    row->setSpacing(6);

    // { texto del boton, comando del firmware, color opcional }
    struct Btn { QString text; QString cmd; QString color; };
    const QList<Btn> defs = {
        { QStringLiteral("⏪"), QStringLiteral("REW"),   QString() },            // ⏪
        { QStringLiteral("▶"), QStringLiteral("PLAY"),  QStringLiteral("#2e7d32") }, // ▶
        { QStringLiteral("⏸"), QStringLiteral("PAUSE"), QString() },            // ⏸
        { QStringLiteral("⏹"), QStringLiteral("STOP"),  QString() },            // ⏹
        { QStringLiteral("⏩"), QStringLiteral("FF"),    QString() },            // ⏩
        { QStringLiteral("●"), QStringLiteral("REC"),   QStringLiteral("#c0392b") }, // ●
    };

    for (const Btn &d : defs)
    {
        auto *btn = new QPushButton(d.text, group);
        btn->setMinimumHeight(64);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = btn->font();
        f.setPointSize(f.pointSize() + 8);
        btn->setFont(f);
        btn->setToolTip(d.cmd);
        if (!d.color.isEmpty())
            btn->setStyleSheet(QStringLiteral("QPushButton { color: %1; font-weight: bold; }").arg(d.color));

        const QString cmd = d.cmd;
        connect(btn, &QPushButton::clicked, this, [this, cmd]{ send(cmd); });
        row->addWidget(btn);
    }

    return group;
}

// ---------------------------------------------------------------------------
// Panel LOCATE: minutos y segundos (la hora va fija en 00) + "IR" e "INICIO".
// ---------------------------------------------------------------------------
QWidget *RCHD24::buildLocatePanel()
{
    auto *group = new QGroupBox(tr("Localizar (LOCATE)"), this);
    auto *row   = new QHBoxLayout(group);
    row->setSpacing(6);

    m_editMinutes = new QLineEdit(QStringLiteral("0"), group);
    m_editMinutes->setValidator(new QIntValidator(0, 999, m_editMinutes));
    m_editMinutes->setAlignment(Qt::AlignCenter);
    m_editMinutes->setMinimumHeight(48);
    m_editMinutes->setMaximumWidth(90);

    m_editSeconds = new QLineEdit(QStringLiteral("0"), group);
    m_editSeconds->setValidator(new QIntValidator(0, 59, m_editSeconds));
    m_editSeconds->setAlignment(Qt::AlignCenter);
    m_editSeconds->setMinimumHeight(48);
    m_editSeconds->setMaximumWidth(90);

    auto *go = new QPushButton(tr("IR"), group);
    go->setMinimumHeight(48);
    connect(go, &QPushButton::clicked, this, &RCHD24::onLocateGo);

    auto *home = new QPushButton(tr("INICIO"), group);
    home->setMinimumHeight(48);
    connect(home, &QPushButton::clicked, this, [this]{ send(QStringLiteral("LOCATE0")); });

    row->addWidget(new QLabel(tr("Min:"), group));
    row->addWidget(m_editMinutes);
    row->addWidget(new QLabel(tr("Seg:"), group));
    row->addWidget(m_editSeconds);
    row->addStretch(1);
    row->addWidget(go);
    row->addWidget(home);

    return group;
}

// ---------------------------------------------------------------------------
// Acciones
// ---------------------------------------------------------------------------
void RCHD24::onArmToggled(int track, bool on)
{
    // Usamos la forma explicita ON/OFF (no el toggle) para que el estado del
    // boton y el de la HD24 no se desincronicen si se pierde un comando.
    send(QStringLiteral("ARM %1 %2").arg(track).arg(on ? QStringLiteral("ON")
                                                       : QStringLiteral("OFF")));
}

void RCHD24::onLocateGo()
{
    const int mm = m_editMinutes->text().toInt();
    const int ss = m_editSeconds->text().toInt();
    // Formato del firmware: hh:mm:ss:ff. La hora va fija en 00 y los frames en 0.
    send(QStringLiteral("LOCATE 00:%1:%2:00")
             .arg(mm, 2, 10, QLatin1Char('0'))
             .arg(ss, 2, 10, QLatin1Char('0')));
}

// ---------------------------------------------------------------------------
// Envio + estado
// ---------------------------------------------------------------------------
bool RCHD24::send(const QString &command)
{
    if (m_active == Active::Tcp && m_tcp->isConnected())
        return m_tcp->sendCommand(command);
    if (m_active == Active::Bluetooth && m_bt->isConnected())
        return m_bt->sendCommand(command);

    statusBar()->showMessage(tr("No conectado — usa el menu Conexion (comando ignorado: %1)")
                                 .arg(command), 4000);
    return false;
}

void RCHD24::setConnected(bool connected, const QString &via)
{
    if (connected)
        m_statusLabel->setText(tr("Conectado (%1)").arg(via));
    else
        m_statusLabel->setText(tr("Desconectado"));

    // Los controles operativos solo tienen sentido con conexion activa; el
    // header (boton Conexion) queda siempre habilitado.
    if (m_controls)
        m_controls->setEnabled(connected);
}

void RCHD24::applyArmState(const QString &csv)
{
    // csv = "1,3,5" (o "-" si no hay ninguna). Marcamos exactamente esas pistas.
    QList<int> armed;
    if (csv != QLatin1String("-"))
        for (const QString &tok : csv.split(QLatin1Char(','), Qt::SkipEmptyParts))
            armed.append(tok.trimmed().toInt());

    for (int i = 0; i < m_armButtons.size(); ++i)
    {
        QPushButton *btn = m_armButtons[i];
        const bool on = armed.contains(i + 1);
        // Evitamos reenviar comandos mientras sincronizamos el estado visual.
        QSignalBlocker block(btn);
        btn->setChecked(on);
    }
}

// ---------------------------------------------------------------------------
// Eventos de los transportes
// ---------------------------------------------------------------------------
void RCHD24::onConnected(const QString &via)
{
    setConnected(true, via);
    statusBar()->showMessage(tr("Conectado por %1").arg(via), 3000);
    // Pedimos el estado de pistas para sincronizar los botones ARM.
    send(QStringLiteral("ARMSTATE"));
}

void RCHD24::onDisconnected()
{
    m_active = Active::None;
    setConnected(false);
    statusBar()->showMessage(tr("Conexion cerrada"), 3000);
}

void RCHD24::onLineReceived(const QString &line)
{
    statusBar()->showMessage(line, 3000);

    // Sincronizamos los botones ARM cuando llega el estado de pistas.
    // Respuesta del firmware: "OK ARMSTATE 1,3,5".
    static const QString tag = QStringLiteral("ARMSTATE ");
    const int idx = line.indexOf(tag);
    if (idx >= 0)
        applyArmState(line.mid(idx + tag.size()).trimmed());
}

void RCHD24::onTransportError(const QString &message)
{
    statusBar()->showMessage(tr("Error: %1").arg(message), 5000);
}
