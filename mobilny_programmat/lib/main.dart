import 'dart:async';
import 'dart:typed_data';
import 'package:flutter/material.dart';
import 'package:usb_serial/usb_serial.dart';
import 'package:usb_serial/transaction.dart';

void main() => runApp(const StalkerProgrammerApp());

class StalkerProgrammerApp extends StatelessWidget {
  const StalkerProgrammerApp({super.key});
  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'S.T.A.L.K.E.R. Programmer',
      debugShowCheckedModeBanner: false,
      theme: ThemeData.dark().copyWith(
        colorScheme: ColorScheme.dark(
          primary: const Color(0xFF4CAF50),
          secondary: const Color(0xFFFFA726),
          surface: const Color(0xFF1A1A2E),
        ),
        scaffoldBackgroundColor: const Color(0xFF0F0F1A),
        appBarTheme: const AppBarTheme(
          backgroundColor: Color(0xFF16213E),
          elevation: 0,
        ),
        cardTheme: const CardTheme(
          color: Color(0xFF1A1A2E),
          elevation: 4,
        ),
      ),
      home: const ProgrammerScreen(),
    );
  }
}

// ─── Типы устройств (совпадают с programmer.py) ───
enum DeviceType { anomaly, safeZone, pdaConfig }

class ProgrammerScreen extends StatefulWidget {
  const ProgrammerScreen({super.key});
  @override
  State<ProgrammerScreen> createState() => _ProgrammerScreenState();
}

class _ProgrammerScreenState extends State<ProgrammerScreen> {
  UsbPort? _port;
  UsbDevice? _device;
  String _status = 'Отключено';
  Color _statusColor = Colors.red;
  String _log = '';
  DeviceType _devType = DeviceType.anomaly;
  String _deviceName = '';

  // ─── АНОМАЛИЯ ───
  int _dmgMask = 1;
  final _dmgCtl = TextEditingController(text: '10');
  final _freqCtl = TextEditingController(text: '10');
  final _radDmgCtl = TextEditingController(text: '0');
  final _radFrqCtl = TextEditingController(text: '0');

  // ─── УБЕЖИЩЕ ───
  final _szHpCtl = TextEditingController(text: '5');
  final _szRadCtl = TextEditingController(text: '2');
  final _szHpFrqCtl = TextEditingController(text: '10');
  final _szRadFrqCtl = TextEditingController(text: '15');
  bool _szEmission = false;
  List<TextEditingController> _szProtCtls = [];

  // ─── ПДА КОНФИГ ───
  int _pdaFuncFlags = 0xFF;
  final _pdaMaxHpCtl = TextEditingController(text: '100');
  final _pdaStartHpCtl = TextEditingController(text: '100');
  final _pdaMaxRadCtl = TextEditingController(text: '200');
  final _pdaMoneyCtl = TextEditingController(text: '500');
  final _pdaLvlCtl = TextEditingController(text: '1');
  final _pdaXpCtl = TextEditingController(text: '0');
  List<TextEditingController> _pdaProtCtls = [];

  final List<String> _dmgNames = [
    'ВЗРЫВ', 'КРОВЬ', 'ТЕРМО', 'ЭЛЕКТРО', 'ХИМИЯ', 'ПСИ', 'ГРАВИТ.'
  ];
  final List<String> _pdaFuncNames = [
    'HP', 'RAD', 'Деньги', 'Броня', 'Арты', 'Аномалии', 'Уровни', 'Расходники'
  ];

  @override
  void initState() {
    super.initState();
    _szProtCtls = List.generate(8, (_) => TextEditingController(text: '0'));
    _pdaProtCtls = List.generate(8, (_) => TextEditingController(text: '0'));
    UsbSerial.usbEventStream?.listen(_onUsbEvent);
    _refreshDevices();
  }

  void _onUsbEvent(UsbEvent event) {
    _refreshDevices();
  }

  Future<void> _refreshDevices() async {
    final devices = await UsbSerial.listDevices();
    if (devices.isNotEmpty && _port == null) {
      _connectDevice(devices.first);
    }
  }

  Future<void> _connectDevice(UsbDevice device) async {
    _port?.close();
    final port = await device.create();
    if (port == null) {
      _addLog('Ошибка: не удалось открыть порт');
      return;
    }
    final ok = await port.open();
    if (!ok) {
      _addLog('Ошибка: порт не открылся');
      return;
    }
    await port.setDTR(true);
    await port.setRTS(true);
    await port.setPortParameters(
      115200, UsbPort.DATABITS_8, UsbPort.STOPBITS_1, UsbPort.PARITY_NONE,
    );

    _port = port;
    _device = device;
    setState(() {
      _status = 'Подключено: ${device.productName ?? "ESP32"}';
      _statusColor = Colors.green;
    });

    // Слушаем ответы
    port.inputStream?.listen((data) {
      final text = String.fromCharCodes(data).trim();
      if (text.isNotEmpty) _addLog('← $text');
    });

    // Определяем тип устройства
    _sendCommand('STALKER_WHO');
  }

  void _addLog(String msg) {
    setState(() {
      _log = '$msg\n$_log';
      if (_log.length > 2000) _log = _log.substring(0, 2000);
      // Определяем устройство из ответа
      if (msg.contains('STALKER:ANOMALY') || msg.contains('STALKER:SAFE_ZONE')) {
        _deviceName = msg.contains('ANOMALY') ? 'Полевое устр.' : 'Полевое устр.';
      } else if (msg.contains('STALKER:PDA')) {
        _deviceName = 'ПДА';
      }
    });
  }

  Future<void> _sendCommand(String cmd) async {
    if (_port == null) {
      _addLog('Нет подключения!');
      return;
    }
    _addLog('→ $cmd');
    await _port!.write(Uint8List.fromList('$cmd\n'.codeUnits));
  }

  void _disconnect() {
    _port?.close();
    _port = null;
    _device = null;
    setState(() {
      _status = 'Отключено';
      _statusColor = Colors.red;
      _deviceName = '';
    });
  }

  // ─── СБОРКА CONFIG СТРОК ───
  String _buildAnomalyConfig() {
    return 'CONFIG_WRITE:cat=0,'
        'dmg_mask=${_dmgMask},'
        'dmg=${_dmgCtl.text},'
        'freq=${_freqCtl.text},'
        'rad_dmg=${_radDmgCtl.text},'
        'rad_frq=${_radFrqCtl.text}';
  }

  String _buildSafeZoneConfig() {
    final prots = _szProtCtls.map((c) => c.text).join(',');
    return 'CONFIG_WRITE:cat=1,'
        'sz_hp=${_szHpCtl.text},'
        'sz_hp_frq=${_szHpFrqCtl.text},'
        'sz_rad=${_szRadCtl.text},'
        'sz_rad_frq=${_szRadFrqCtl.text},'
        'sz_emit=${_szEmission ? 1 : 0},'
        'sz_prot=$prots';
  }

  String _buildPdaFuncConfig() {
    return 'CONFIG:FUNC:flags=$_pdaFuncFlags';
  }

  String _buildPdaPresetConfig() {
    final prots = List.generate(8, (i) => 'r$i=${_pdaProtCtls[i].text}').join(',');
    return 'CONFIG:PRESET:'
        'maxhp=${_pdaMaxHpCtl.text},'
        'starthp=${_pdaStartHpCtl.text},'
        'maxrad=${_pdaMaxRadCtl.text},'
        'money=${_pdaMoneyCtl.text},'
        'lvl=${_pdaLvlCtl.text},'
        'xp=${_pdaXpCtl.text},'
        '$prots';
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Row(
          children: [
            Icon(Icons.memory, color: Color(0xFF4CAF50)),
            SizedBox(width: 8),
            Text('S.T.A.L.K.E.R. PROGRAMMER',
                style: TextStyle(fontWeight: FontWeight.bold, letterSpacing: 2)),
          ],
        ),
        actions: [
          if (_port != null)
            IconButton(
              icon: const Icon(Icons.usb_off, color: Colors.red),
              onPressed: _disconnect,
            ),
          if (_port == null)
            IconButton(
              icon: const Icon(Icons.usb, color: Colors.grey),
              onPressed: _refreshDevices,
            ),
        ],
      ),
      body: Column(
        children: [
          // Статус-бар
          Container(
            width: double.infinity,
            padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
            color: _statusColor.withOpacity(0.15),
            child: Row(
              children: [
                Icon(Icons.circle, size: 10, color: _statusColor),
                const SizedBox(width: 8),
                Text(_status, style: TextStyle(color: _statusColor)),
                const Spacer(),
                if (_deviceName.isNotEmpty)
                  Chip(
                    label: Text(_deviceName),
                    backgroundColor: const Color(0xFF16213E),
                  ),
              ],
            ),
          ),

          // Переключатель устройств
          Padding(
            padding: const EdgeInsets.all(8.0),
            child: SegmentedButton<DeviceType>(
              segments: const [
                ButtonSegment(value: DeviceType.anomaly, label: Text('АНОМАЛИЯ'), icon: Icon(Icons.warning)),
                ButtonSegment(value: DeviceType.safeZone, label: Text('УБЕЖИЩЕ'), icon: Icon(Icons.shield)),
                ButtonSegment(value: DeviceType.pdaConfig, label: Text('ПДА'), icon: Icon(Icons.phone_android)),
              ],
              selected: {_devType},
              onSelectionChanged: (s) => setState(() => _devType = s.first),
            ),
          ),

          // Параметры
          Expanded(
            child: SingleChildScrollView(
              padding: const EdgeInsets.all(12),
              child: _buildParams(),
            ),
          ),

          // Кнопки действий
          Container(
            padding: const EdgeInsets.all(12),
            child: Row(
              children: [
                Expanded(
                  child: ElevatedButton.icon(
                    style: ElevatedButton.styleFrom(
                      backgroundColor: const Color(0xFF4CAF50),
                      padding: const EdgeInsets.symmetric(vertical: 14),
                    ),
                    icon: const Icon(Icons.upload),
                    label: const Text('ПРОШИТЬ', style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold)),
                    onPressed: _onFlash,
                  ),
                ),
                const SizedBox(width: 12),
                ElevatedButton.icon(
                  style: ElevatedButton.styleFrom(
                    backgroundColor: const Color(0xFF1565C0),
                    padding: const EdgeInsets.symmetric(vertical: 14, horizontal: 16),
                  ),
                  icon: const Icon(Icons.download),
                  label: const Text('ЧИТАТЬ'),
                  onPressed: () => _sendCommand('CONFIG_READ'),
                ),
              ],
            ),
          ),

          // Лог
          Container(
            height: 120,
            width: double.infinity,
            margin: const EdgeInsets.fromLTRB(12, 0, 12, 12),
            padding: const EdgeInsets.all(8),
            decoration: BoxDecoration(
              color: Colors.black,
              borderRadius: BorderRadius.circular(8),
              border: Border.all(color: const Color(0xFF333333)),
            ),
            child: SingleChildScrollView(
              child: Text(_log, style: const TextStyle(
                fontFamily: 'monospace', fontSize: 11, color: Color(0xFF00FF00),
              )),
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildParams() {
    switch (_devType) {
      case DeviceType.anomaly:
        return _buildAnomalyParams();
      case DeviceType.safeZone:
        return _buildSafeZoneParams();
      case DeviceType.pdaConfig:
        return _buildPdaParams();
    }
  }

  void _onFlash() {
    switch (_devType) {
      case DeviceType.anomaly:
        _sendCommand(_buildAnomalyConfig());
        break;
      case DeviceType.safeZone:
        _sendCommand(_buildSafeZoneConfig());
        break;
      case DeviceType.pdaConfig:
        _sendCommand(_buildPdaFuncConfig());
        Future.delayed(const Duration(milliseconds: 300), () {
          _sendCommand(_buildPdaPresetConfig());
        });
        break;
    }
  }

  // ─── UI: АНОМАЛИЯ ───
  Widget _buildAnomalyParams() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('⚡ АНОМАЛИЯ', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Color(0xFFFF5722))),
            const SizedBox(height: 12),
            const Text('Типы урона:', style: TextStyle(color: Colors.grey)),
            Wrap(
              spacing: 6,
              children: List.generate(7, (i) => FilterChip(
                label: Text(_dmgNames[i]),
                selected: _dmgMask & (1 << i) != 0,
                selectedColor: const Color(0xFF4CAF50).withOpacity(0.3),
                onSelected: (v) => setState(() {
                  if (v) _dmgMask |= (1 << i); else _dmgMask &= ~(1 << i);
                }),
              )),
            ),
            const SizedBox(height: 12),
            _numField('Урон (HP)', _dmgCtl),
            _numField('Интервал (сек)', _freqCtl),
            const Divider(),
            _numField('RAD за удар', _radDmgCtl),
            _numField('RAD интервал (сек)', _radFrqCtl),
          ],
        ),
      ),
    );
  }

  // ─── UI: УБЕЖИЩЕ ───
  Widget _buildSafeZoneParams() {
    return Card(
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text('🛡 УБЕЖИЩЕ', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Color(0xFF4CAF50))),
            const SizedBox(height: 12),
            _numField('Лечение HP/тик', _szHpCtl),
            _numField('Интервал HP (сек)', _szHpFrqCtl),
            _numField('Снятие RAD/тик', _szRadCtl),
            _numField('Интервал RAD (сек)', _szRadFrqCtl),
            SwitchListTile(
              title: const Text('Защита от выброса'),
              value: _szEmission,
              onChanged: (v) => setState(() => _szEmission = v),
            ),
            const Divider(),
            const Text('Защиты %:', style: TextStyle(color: Colors.grey)),
            ..._buildProtGrid(_szProtCtls),
          ],
        ),
      ),
    );
  }

  // ─── UI: ПДА КОНФИГ ───
  Widget _buildPdaParams() {
    return Column(
      children: [
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('📱 ПДА — ФУНКЦИИ', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Color(0xFF2196F3))),
                const SizedBox(height: 8),
                Wrap(
                  spacing: 6,
                  children: List.generate(8, (i) => FilterChip(
                    label: Text(_pdaFuncNames[i]),
                    selected: _pdaFuncFlags & (1 << i) != 0,
                    selectedColor: const Color(0xFF2196F3).withOpacity(0.3),
                    onSelected: (v) => setState(() {
                      if (v) _pdaFuncFlags |= (1 << i); else _pdaFuncFlags &= ~(1 << i);
                    }),
                  )),
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 8),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                const Text('📊 ПДА — ПРЕСЕТ', style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold, color: Color(0xFF2196F3))),
                const SizedBox(height: 12),
                _numField('Макс. HP', _pdaMaxHpCtl),
                _numField('Старт HP', _pdaStartHpCtl),
                _numField('Макс. RAD', _pdaMaxRadCtl),
                _numField('Деньги', _pdaMoneyCtl),
                _numField('Уровень', _pdaLvlCtl),
                _numField('Опыт (XP)', _pdaXpCtl),
                const Divider(),
                const Text('Базовые защиты %:', style: TextStyle(color: Colors.grey)),
                ..._buildProtGrid(_pdaProtCtls),
              ],
            ),
          ),
        ),
      ],
    );
  }

  List<Widget> _buildProtGrid(List<TextEditingController> ctls) {
    final labels = ['ВЗРЫВ', 'КРОВЬ', 'ТЕРМО', 'ЭЛЕКТРО', 'ХИМИЯ', 'ПСИ', 'ГРАВИТ.', 'RAD'];
    final rows = <Widget>[];
    for (int i = 0; i < 8; i += 2) {
      rows.add(Row(
        children: [
          Expanded(child: _numField(labels[i], ctls[i])),
          const SizedBox(width: 8),
          Expanded(child: _numField(labels[i + 1], ctls[i + 1])),
        ],
      ));
    }
    return rows;
  }

  Widget _numField(String label, TextEditingController ctl) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: TextField(
        controller: ctl,
        keyboardType: TextInputType.number,
        decoration: InputDecoration(
          labelText: label,
          filled: true,
          fillColor: const Color(0xFF0F0F1A),
          border: OutlineInputBorder(borderRadius: BorderRadius.circular(8)),
          contentPadding: const EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          isDense: true,
        ),
      ),
    );
  }
}
