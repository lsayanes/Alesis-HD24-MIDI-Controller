
#pragma once

#include <QMainWindow>
#include <QVector>
#include <string>

#include "Tcp.h"
#include "Bluetooth.h"

class QPushButton;
class QLineEdit;
class QLabel;
class QMenu;

// ---------------------------------------------------------------------------
// RCHD24 — ventana principal del control remoto de la Alesis ADAT HD24.
// ---------------------------------------------------------------------------
//
// La app habla con el firmware ESP32 (hd24_midi_controller.ino) por uno de dos
// transportes intercambiables: Tcp (WiFi) o Bluetooth (SPP). Ambos exponen la
// misma interfaz (sendCommand + senales connected/disconnected/lineReceived/
// errorOccurred), asi que la UI no distingue cual esta activo: solo llama a
// send() y el metodo despacha al transporte conectado.
//
// La configuracion de conexion vive en el menu "Conexion" para que la pantalla
// principal quede solo con lo util: los 24 botones ARM, el transporte
// (REW/PLAY/PAUSE/STOP/FF/REC) y el LOCATE (minutos y segundos).

class RCHD24 : public QMainWindow
{
    Q_OBJECT

public:
    explicit RCHD24(QWidget *parent = nullptr);
    ~RCHD24() override = default;

    // Cantidad de pistas de la HD24 (coincide con NUM_TRACKS del firmware).
    static constexpr int NUM_TRACKS = 24;

    // Construye menu + widgets centrales. Devuelve true si quedo lista.
    bool create(const std::string &title);

private slots:
    // Transporte / LOCATE.
    void onArmToggled(int track, bool on);   // track: 1..24
    void onLocateGo();                        // LOCATE con los min/seg del form

    // Menu de conexion.
    void connectTcpDialog();
    void connectBluetoothDialog();
    void disconnectActive();

    // Eventos de los transportes.
    void onConnected(const QString &via);
    void onDisconnected();
    void onLineReceived(const QString &line);
    void onTransportError(const QString &message);

private:
    // Cual de los dos transportes esta activo (el que recibe los comandos).
    enum class Active { None, Tcp, Bluetooth };

    // Construccion de la UI.
    QWidget *buildHeader();          // boton "Conexion" + estado (siempre visible)
    QWidget *buildArmPanel();
    QWidget *buildTransportPanel();
    QWidget *buildLocatePanel();
    QMenu   *buildConnectionMenu();  // menu desplegable de conexion

    // Envia un comando por el transporte activo. Muestra aviso si no hay conexion.
    bool send(const QString &command);

    // Refleja el estado de conexion en la barra de estado y en los controles.
    void setConnected(bool connected, const QString &via = QString());

    // Sincroniza los 24 botones ARM a partir de una respuesta "ARMSTATE 1,3,5".
    void applyArmState(const QString &csv);

    // Transportes (uno de los dos queda activo segun el menu).
    Tcp       *m_tcp = nullptr;
    Bluetooth *m_bt  = nullptr;
    Active     m_active = Active::None;

    // Widgets que necesitamos tocar despues de construirlos.
    QWidget   *m_controls = nullptr;       // paneles operativos (se (des)habilitan)
    QVector<QPushButton *> m_armButtons;   // indice 0 -> pista 1
    QLineEdit *m_editMinutes = nullptr;
    QLineEdit *m_editSeconds = nullptr;
    QLabel    *m_statusLabel = nullptr;
};
