# 浜ф祴鍥轰欢骞冲彴鍖栧紑鍙戣褰?
> 璁板綍璁捐璇勫鍐崇瓥銆佸疄鐜拌繃绋嬮櫡闃便€佸緟鍔炵姸鎬併€?> 涓庤璁℃枃妗ｏ紙specs/锛夊拰瀹炵幇璁″垝锛坧lans/锛変簰琛モ€斺€攕pec 璁?搴旇鏄粈涔堟牱"锛宲lan 璁?鎬庝箞涓€姝ユ鍋?锛宒evlog 璁?瀹為檯鍙戠敓浜嗕粈涔?銆?
---

## 鐩綍

- [闃舵 1锛氶鏋舵惌寤篯(#闃舵-1楠ㄦ灦鎼缓)
- [闃舵 2锛氶┍鍔ㄨВ鑰?+ 璋冨害閾捐矾璐€歖(#闃舵-2椹卞姩瑙ｈ€?-璋冨害閾捐矾璐€?

---

## 闃舵 1锛氶鏋舵惌寤?
### 1.1 璁捐杩唬璇︽儏

#### 1.1.1 v1.0 鈫?v1.1锛氱嫭绔嬫€ч噸鏋?
**瑙﹀彂鍘熷洜**锛氱敤鎴锋彁鍑烘牳蹇冧笉鍙樺紡鈥斺€?factory_fw 鏄彲鏁翠綋鎼蛋鐨勯」鐩牴"銆?
**鍏蜂綋鍙樻洿**锛?| 鍙樻洿椤?| v1.0 | v1.1 | 鏂囦欢浣嶇疆 |
|--------|------|------|----------|
| build.sh 浣嶇疆 | `FACTORY_GIT/build.sh` | `factory_fw/build.sh` | 鏍圭洰褰?鈫?`factory_fw/` |
| cmake/ 浣嶇疆 | `FACTORY_GIT/cmake/` | `factory_fw/cmake/` | 鏍圭洰褰?鈫?`factory_fw/` |
| build/ 浜х墿 | `FACTORY_GIT/build/` | `factory_fw/build/` | 鏍圭洰褰?鈫?`factory_fw/` |
| main.cpp 浣嶇疆 | `factory_fw/main.cpp` | `factory_fw/src/main.cpp` | 鏍圭洰褰?鈫?`src/` |
| 鏃т唬鐮佸叧绯?| "涓庣幇鏈?src/include 骞惰" | "浠呰縼绉绘湡鍙鍙傝€冿紝闆舵瀯寤轰緷璧? | 璁捐鏂囨。鎺緸 |
| FW_ROOT 浼犲叆 | `build.sh` 浼?`-DFW_ROOT` | 骞冲彴 CMake 鐢?`if(NOT DEFINED FW_ROOT)` 鍥為€€ | `build.sh` + `platforms/*/CMakeLists.txt` |
| build.sh 閿氬畾 | 鐩稿璺緞锛屼緷璧?CWD | `SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"` | `factory_fw/build.sh:5-6` |

**鏂板绔犺妭**锛?- 绗?7 鑺傘€屼緷璧栫鐞嗐€嶏細宸ュ叿閾句粠 `FALCON_SDK` 鐜鍙橀噺瀹氫綅
- 绗?8 鑺傘€岀嫭绔嬫€у畧鍗€嶏細CI 闅旂鏋勫缓娴嬭瘯

#### 1.1.2 v1.1 鈫?v1.2锛氬弻娉ㄥ唽璇箟淇

**瑙﹀彂鍘熷洜**锛氱敤鎴锋寚鍑?v1.1 鐨?`IPlatform` 鍚屾椂淇濈暀 `createXxx()` + `registerDrivers()` 涓?`.docx` 鏉冨▉鏉ユ簮涓嶄竴鑷达紝涓斿紩鍏ュ啑浣欍€?
**璇勫鎸囧嚭鐨勫叿浣撻棶棰?*锛?1. `IPlatform` 9 涓?`createXxx()` 鍏ㄦ槸绾櫄 (`= 0`)锛屼絾 `main.cpp` 瀹為檯鍙皟浜?`createDisplayDriver()` 涓€涓紝鍓╀笅 7 涓槸绾鎺ュ彛
2. `createCameraDriver()` 杩?`Gc4663`锛宍registerDrivers()` 鍙?`bind<ICameraDriver>` 鍒?`Gc4663`鈥斺€旀崲 sensor 瑕佹敼涓ゅ锛屾棤鏈哄埗淇濊瘉涓€鑷?
**鍐崇瓥杩囩▼**锛?- 閫夐」 (a)锛氱函 registry锛宍main` 鐢?`reg.create<IDisplayDriver>()` 鍙栦竴娆¤嚜鎸?- 閫夐」 (b)锛氬彧缁?display/recorder 涓や釜鐣?`createXxx()`锛屽叾浣?7 涓垹鎺?- **鏈€缁堥€夋嫨 (a)**锛氬叏閮ㄨ蛋 registry锛屽崟涓€鐪熺浉婧?
**浠ｇ爜鍙樻洿**锛?```cpp
// v1.1 IPlatform.h锛堝凡搴熷純锛?class IPlatform {
    virtual std::unique_ptr<IEncoder>       createEncoder()       = 0;
    virtual std::unique_ptr<ICameraDriver>  createCameraDriver()  = 0;
    // ... 鍏?9 涓?createXxx()
    virtual void registerDrivers(DriverRegistry& reg) = 0;
};

// v1.2 IPlatform.h锛堝綋鍓嶏級
class IPlatform {
    virtual void registerDrivers(DriverRegistry& reg) = 0;
    // 浠呮涓€涓娊璞℃柟娉?};
```

**main.cpp 瀵瑰簲鍙樻洿**锛?```cpp
// v1.1
auto display = platform->createDisplayDriver();

// v1.2
auto display = reg.create<IDisplayDriver>();
```

#### 1.1.3 v1.2 鈫?v1.3-final锛氭妧鏈慨澶?
**瑙﹀彂鍘熷洜**锛氫唬鐮佸鏌ュ彂鐜扮紪璇戞湡鍜岃繍琛屾湡纭激銆?
浠ヤ笅淇鎸夈€岀姸鎬併€嶅垎绫烩€斺€旇璁¤瘎瀹￠樁娈靛彂鐜扮殑闂 vs 缂栫爜瀹炵幇闃舵鏆撮湶鐨勯棶棰橈細

**璁捐璇勫闃舵鍙戠幇骞跺喕缁擄紙v1.2 涔嬪墠锛?*

| # | 淇椤?| 鐘舵€?| 璇存槑 |
|---|--------|------|------|
| 1 | TestContext::params_ 鍊兼寔鏈?| 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| 璇勫鍙戠幇鏋勯€犲嚱鏁颁复鏃跺璞＄粦瀹氬紩鐢ㄦ垚鍛樼殑 UB锛宍TestContext.h` 宸叉敼涓?`json params_` |
| 2 | async lambda move-capture | 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| 璇勫鍙戠幇 `unique_ptr` 涓嶅彲鎷疯礉锛宍AsyncTaskQueue.h` 宸茬敤 `shared_ptr` 鑷寔 |
| 3 | OBJECT library 闃?dead-strip | 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| 璇勫鍙戠幇闈欐€佸簱鑷敞鍐岄潤榛樻秷澶憋紝`src/tests/CMakeLists.txt` 宸叉敼 OBJECT |

**缂栫爜瀹炵幇闃舵鏆撮湶锛圥hase 1 楠ㄦ灦鎼缓鏃讹級**

| # | 淇椤?| 鐘舵€?| 璇存槑 |
|---|--------|------|------|
| 4 | enqueueAndWait shared_ptr 绔炴€?| 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| `AsyncTaskQueue.h` 宸插疄鐜?`shared_ptr` 鐗堬紝`factory_test` 閾炬帴閫氳繃 |
| 5 | loadTestConfig 鑱岃矗褰掍綅 + 鍚姩鏈熸牎楠?| 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| `TestEngine.cpp:23-37` 宸插疄鐜?module/topic 瀛樺湪鎬ф鏌?|
| 6 | catch(...) 寮傚父瀹堝崼 | 璁捐宸插畾 | 鍐荤粨鍦?spec 鐨?`dispatch()` 瀹氫箟涓紝**瀹為檯浠ｇ爜浠嶄负 `// TODO` 妗?*锛屽緟 Phase 2 瀹炵幇 |
| 7 | SysfsGpioDriver 鍘婚噸 | 璁捐宸插畾 / 浠ｇ爜宸插疄鐜?/ 宸茬紪璇戦獙璇?| 鍙暀 `src/platforms/common/` 涓€浠斤紝falcon CMake 鏈紩鐢ㄧ浜屼唤 |

**鍏抽敭鍖哄垎**锛氫慨澶?6锛坈atch(...)锛夊瘎鐢熷湪 `dispatch()` 鍐呴儴锛岃€?`dispatch()` 鍦?Phase 1 鏄┖妗┿€傚畠鐨勫紓甯稿畧鍗涔夊凡鍐荤粨鍦?v1.3-final 璁捐鏂囨。涓紝浣?*瀹為檯浠ｇ爜灏氭湭钀藉湴**銆?
### 1.2 Phase 1 瀹炵幇璇︽儏

#### 1.2.1 鎻愪氦鍘嗗彶

```
d8578a8 doc(v1.3-final): 浜ф祴鍥轰欢骞冲彴鍖栬璁℃枃妗?鈥?鍐荤粨鍩虹嚎
d423331 doc(v1.3): 浜ф祴鍥轰欢骞冲彴鍖栬璁℃枃妗?fcca28d doc(v1.2): 浜ф祴鍥轰欢骞冲彴鍖栬璁℃枃妗?ec1e833 doc(v1.1): 浜ф祴鍥轰欢骞冲彴鍖栬璁℃枃妗?de1b918 doc(v1.0): 浜ф祴鍥轰欢骞冲彴鍖栬璁℃枃妗?(Section 1-3)
f389216 feat: Phase 1 skeleton 鈥?all interfaces, registries, stubs, build system
1e633b0 fix: compilation fixes for Base platform
8e3c07a doc: add development log (devlog.md)
d8260e1 doc: 瀹屽杽寮€鍙戣褰曪紝琛ュ厖璁捐杩唬璇︽儏銆佺紪璇戜慨澶嶅疄褰曘€佷唬鐮?diff
```

#### 1.2.2 null 骞冲彴缂栬瘧楠岃瘉瀹炲綍

**鐜**锛歐SL2, Ubuntu 22.04, GCC 11.4.0, CMake 3.22

**楠岃瘉鍛戒护**锛?```bash
cd /home/gdh/FACTORY_GIT/factory_fw
PLATFORM=base ./build.sh
```

**鏈€缁堣緭鍑?*锛?```
[100%] Linking CXX executable factory_test
[100%] Built target factory_test
[build_factory] Base platform 鈥?nothing to package
```

**淇杞**锛氬叡 4 杞紪璇戞墠閫氳繃

**Round 1锛歯lohmann/json 缂哄け**
```
fatal error: nlohmann/json.hpp: No such file or directory
```
**淇**锛歚curl -o third_party/nlohmann/json.hpp ...`

**Round 2锛歩nclude 璺緞閿欒**
鏂囦欢鍦?`third_party/nlohmann/json.hpp`锛孋Make 璺緞鏄?`third_party/nlohmann`锛岀紪璇戝櫒鏌ユ壘 `third_party/nlohmann/nlohmann/json.hpp`鈥斺€斾笉鍖归厤銆?**淇**锛? 涓?CMakeLists.txt 涓?`third_party/nlohmann` 鈫?`third_party`

**Round 3锛歁oduleRegistry 绫诲瀷涓嶅尮閰?+ ITestModule 涓嶅畬鏁?*
**淇**锛歭ambda 杞崲 + 琛ュ叏 include

**Round 4锛歮ain.cpp 鍛藉悕绌洪棿 + 绫诲瀷涓嶅尮閰?*
**淇**锛歚using namespace ft;` + `cfg.platformName().c_str()`

### 1.3 Phase 1 寰呭姙娓呭崟鐘舵€?
| 缂栧彿 | 鍐呭 | 鐘舵€?|
|------|------|------|
| A | 鏄惧紡鍒楁簮鏂囦欢 | 鉁?null 缂栬瘧閫氳繃 |
| B | `if(NOT DEFINED FW_ROOT)` | 鉁?null 缂栬瘧閫氳繃 |
| C | `SCRIPT_DIR` 閿氬畾 + toolchain 妫€鏌?| 鉁?杩愯 `build.sh` |
| D | `IWifiManager.h` 浣嶇疆 | 鉁?闃呰鏂囦欢 |
| F | `.at("module").get<string>()` | 鉁?闃呰浠ｇ爜 |
| H | `TestResult` 宸ュ巶鏂规硶 | 鉁?闃呰浠ｇ爜 |
| L | `loadTestConfig` 鍚姩鏈熸牎楠?| 鉁?闃呰浠ｇ爜 |
| I | `FALCON_SDK` 鐜鍙橀噺 | 鈴?闇€ falcon 骞冲彴楠岃瘉 |
| E | 閾炬帴浼犻€掓€?PUBLIC/INTERFACE | 鈴?闇€ falcon 瀹為檯閾炬帴 |

### 1.4 Phase 1 浠ｇ爜涓?TODO 姹囨€?
| 鏂囦欢 | TODO | 褰卞搷 |
|------|------|------|
| `src/core/TestEngine.cpp:run()` | MQTT loop 闆嗘垚 | 鍥轰欢鏃犳硶鏀舵秷鎭?|
| `src/core/TestEngine.cpp:onMqttMessage()` | topic鈫抰estCfg 鏌ユ壘 | MQTT 娑堟伅鏃犳硶璺敱 |
| `src/core/TestEngine.cpp:dispatch()` | 瀹屾暣 sync/async 娲惧彂 + catch(...) 瀹堝崼 | 娴嬭瘯椤规棤娉曟墽琛?|
| `src/core/TestEngine.cpp:publishResult()` | `mosquitto_publish` + 閿?| 缁撴灉鏃犳硶涓婃姤 |
| 12 涓?`src/tests/*Test.cpp` | 鍏ㄩ儴 `skipped("not implemented")` | 鏃犲疄闄呮祴璇曞姛鑳?|
| `src/ble/*.cpp` | 绌烘々 | BLE 鏈疄鐜?|
| `platforms/falcon/drivers/*.cpp` | 绌烘々 | RK3576 椹卞姩鏈疄鐜?|
| `platforms/falcon/build_factory.sh` | 浠?`echo` | 涓嶇敓鎴愬浐浠堕暅鍍?|

---

## 闃舵 2锛氶┍鍔ㄨВ鑰?+ 璋冨害閾捐矾璐€?
### 2.1 鐩爣

璁?**"椹卞姩娉ㄥ唽 鈫?娴嬭瘯鑾峰彇 鈫?璋冨害鎵ц 鈫?缁撴灉涓婃姤"** 鏁存潯閾捐矾璺戦€氥€?
Phase 1 鐨勯鏋朵腑锛屼互涓嬪叧閿摼璺妭鐐逛粛涓?`// TODO` 鎴栫┖妗╋細
- `FalconPlatform`锛氫笉瀛樺湪锛宖alcon 8 涓?driver 鏁ｈ惤鍦ㄧ┖鏂囦欢涓紝鏃犵粺涓€娉ㄥ唽鍏ュ彛
- `TestEngine::dispatch()`锛氱┖妗?- `TestEngine::publishResult()`锛氱┖妗?- 12 涓祴璇曟ā鍧楋細鍏ㄩ儴 `skipped("not implemented")`

闃舵 2 鐨勭洰鏍囨槸璁╄繖浜涜妭鐐?*鏈€灏忓彲缂栬瘧鍦板姩璧锋潵**锛岀‖浠剁粏鑺傜簿纭疄鐜扮暀缁欏悗缁樁娈点€?
### 2.2 浠诲姟娓呭崟涓庢墽琛岀粨鏋?
| # | 浠诲姟 | 浼樺厛绾?| 鐘舵€?| 鍏抽敭鏂囦欢 |
|---|------|--------|------|----------|
| 1 | 鍒涘缓 FalconPlatform | 楂?| 鉁?| `platforms/falcon/FalconPlatform.cpp` |
| 2 | 濉厖 8 涓?Falcon driver stub | 楂?| 鉁?| `platforms/falcon/drivers/*.h` + `*.cpp` |
| 3 | 鏇存柊 falcon CMakeLists.txt | 楂?| 鉁?| `platforms/falcon/CMakeLists.txt` |
| 4 | 濉厖 12 涓祴璇曟ā鍧楅鏋?| 楂?| 鉁?| `src/tests/*Test.cpp` |
| 5 | 瀹炵幇 TestEngine::dispatch() | 楂?| 鉁?| `src/core/TestEngine.cpp` |
| 6 | 瀹炵幇 TestEngine::publishResult() | 楂?| 鉁?| `src/core/TestEngine.cpp` |
| 7 | 鍙屽钩鍙扮紪璇戦獙璇?| 楂?| 鉁?| `build/base/` + `build/falcon/` |
| 8 | main.cpp 鐜鍙橀噺鏀寔 | 涓?| 鉁?| `src/main.cpp` |
| 9 | null 骞冲彴閰嶇疆琛ラ綈 | 涓?| 鉁?| `platforms/base/platform.json` + `tests.json` |

### 2.3 闄烽槺涓庝慨澶嶅疄褰?
#### 2.3.1 闄烽槺 A锛歚std::unique_ptr<Derived>` 鏃犳硶鏀惧叆 `std::function<std::unique_ptr<Base>()>`

**鏆撮湶浣嶇疆**锛歚FalconPlatform::registerDrivers()` 鍒濇瀹炵幇鏃?
**闂浠ｇ爜**锛?```cpp
reg.bind<IEncoder>([] { return std::make_unique<RkMppEncoder>(); });
// 閿欒锛歭ambda 杩斿洖 unique_ptr<RkMppEncoder>锛屼笉鑳介殣寮忚浆涓?//        function<unique_ptr<IEncoder>()>
```

**鏍瑰洜**锛歚std::unique_ptr` 涓嶆槸鍗忓彉鐨勩€俙std::function` 鐨勬ā鏉垮弬鏁拌姹傝繑鍥炲€肩被鍨嬬簿纭尮閰嶏紝涓嶅厑璁搁殣寮忚浆鎹€?
**淇鏂规**锛氫慨鏀?`DriverRegistry::bind` 鐨勬ā鏉跨鍚嶏紝浠庢帴鍙?`std::function` 鏀逛负鎺ュ彈閫氱敤 Factory锛?```cpp
// 淇鍓?template<class Interface>
void bind(std::function<std::unique_ptr<Interface>()> factory);

// 淇鍚?template<class Interface, class Factory>
void bind(Factory factory) {
    store_.put(typeid(Interface), [f = std::move(factory)]() -> void* {
        return f().release();
    });
}
```

**楠岃瘉**锛歠alcon 骞冲彴缂栬瘧閫氳繃銆?
#### 2.3.2 闄烽槺 B锛歭ambda 鎹曡幏 `unique_ptr` 鈫?`std::function` 涓嶅彲澶嶅埗

**鏆撮湶浣嶇疆**锛歚TestEngine::dispatch()` 鍒濇瀹炵幇鏃?
**闂浠ｇ爜**锛?```cpp
auto task = [mod = std::move(mod), ctx, topic, this]() mutable -> TestResult {
    return mod->run(ctx);
};
asyncQueue_.enqueue(task);  // 閿欒锛歵ask 鍚?unique_ptr锛屼笉鍙鍒?```

**鏍瑰洜**锛歚std::function` 瑕佹眰鍏剁洰鏍囩被鍨嬪繀椤诲彲澶嶅埗鏋勯€狅紙copy-constructible锛夈€俶ove-only 鐨?lambda 鏃犳硶鏀惧叆 `std::function`銆?
**淇鏂规**锛歚unique_ptr` 杞?`shared_ptr`锛?```cpp
auto modShared = std::shared_ptr<ITestModule>(std::move(mod));
auto task = [modShared, ctx, topic, this]() mutable -> TestResult {
    return modShared->run(ctx);
};
```

**楠岃瘉**锛歯ull 骞冲彴鑷祴閫氳繃銆?
#### 2.3.3 闄烽槺 C锛氶潤鎬佸簱 dead-strip 鍐嶆鍙戜綔锛堝钩鍙版敞鍐岋級

**鏆撮湶浣嶇疆**锛歯ull 骞冲彴杩愯鏃?`createPlatform("null")` 杩斿洖 `nullptr`

**鏍瑰洜**锛歚BasePlatform.cpp` 涓殑 `_reg = registerPlatformFactory("base", ...)` 鎵€鍦ㄧ殑 `.o` 鏂囦欢鏈浠讳綍浠ｇ爜鐩存帴寮曠敤锛岄摼鎺ュ櫒鍋?archive member selection 鏃舵暣涓涪寮冦€?
**澶嶇幇**锛?```bash
nm build/base/factory_test | grep _reg_base
# 鏃犺緭鍑衡€斺€旂鍙疯涓㈠純
```

**淇鏂规**锛氭妸 `BasePlatform` 绫诲畾涔夊拰 `createBasePlatform()` 鍚堝苟鍒?`PlatformFactory.cpp` 鍚屼竴 TU 涓€俙PlatformFactory.o` 鍥犱负鏈?`createPlatform()` 琚?`main.cpp` 寮曠敤锛屾墍浠ヤ笉浼氳涓㈠純锛宍createBasePlatform()` 鍜?`_reg_base` 闅忎箣淇濈暀銆?
```cpp
// PlatformFactory.cpp
// ... createPlatform() 瀹炵幇 ...

class BasePlatform : public IPlatform { ... };
std::unique_ptr<IPlatform> createBasePlatform() { ... }
static bool _reg_base = registerPlatformFactory("base", createBasePlatform);
```

**楠岃瘉**锛歯ull 骞冲彴杩愯鏃?`createPlatform("null")` 鎴愬姛杩斿洖闈炵┖鎸囬拡銆?
**璇勫鍙戠幇 falcon 鍚屾牱涓灙**锛歚nm build/falcon/factory_test | grep _reg_falcon` 闆惰緭鍑衡€斺€擿_reg_falcon` 鍚屾牱琚?archive member selection 涓㈠純銆備箣鍓嶈涓?vtable 寮曠敤淇濈暀鏁翠釜 .o"鐨勬帹鐞嗘槸寰幆鐨勶細vtable 鍙湁鍦?`.o` 琚摼杩涙潵鍚庢墠瀛樺湪锛岃€?`.o` 琚摼杩涙潵鐨勫墠鎻愭槸 `_reg_falcon` 鍏堟妸鏉＄洰濉繘娉ㄥ唽琛ㄢ€︹€?
**缁熶竴淇鏂规**锛氬钩鍙?library 閾炬帴鏃跺姞 `--whole-archive`锛屼笌娴嬭瘯妯″潡璧?OBJECT library 鐨勫璺繚鎸佷竴鑷达紙鍚屼竴鍘熺悊锛屼笉鍚屽疄鐜帮級锛?```cmake
target_link_libraries(factory_test
    factory_core
    factory_common
    ...
    -Wl,--whole-archive
    platform_falcon
    -Wl,--no-whole-archive
    factory_platform_common
)
```
**娉ㄦ剰**锛歚factory_platform_common` 蹇呴』鏀惧湪 `--whole-archive` 涔嬪悗锛屽惁鍒?`FalconPlatform.o` 涓紩鐢?`SysfsGpioDriver` vtable 鏃讹紝閾炬帴鍣ㄥ凡澶勭悊瀹?`factory_platform_common`銆?
**楠岃瘉**锛?```bash
nm build/falcon/factory_test | grep _reg_falcon
# 00000000000c6098 b _ZN2ftL11_reg_falconE  鈫?绗﹀彿瀛樺湪
```

#### 2.3.4 闄烽槺 D锛氬钩鍙?driver include 璺緞缂哄け

**鏆撮湶浣嶇疆**锛歠alcon 骞冲彴棣栨缂栬瘧鏃?
**闂**锛歚platforms/falcon/drivers/*.cpp` 涓殑 `#include "platforms/common/interface/IBatteryDriver.h"` 鎵句笉鍒板ご鏂囦欢銆?
**淇**锛氬湪 `platforms/falcon/CMakeLists.txt` 涓粰 `platform_falcon` target 娣诲姞锛?```cmake
target_include_directories(platform_falcon PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}   # 璁?#include "drivers/xxx.h" 鑳芥壘鍒?    ${FW_ROOT}/include
    ${FW_ROOT}/third_party
)
```

#### 2.3.5 闄烽槺 E锛欼TestModule 涓嶅畬鏁寸被鍨?
**鏆撮湶浣嶇疆**锛歚TestEngine.cpp` 缂栬瘧鏃?
**闂**锛歚TestEngine.cpp` 鍙?`#include "core/ModuleRegistry.h"`锛屽叾涓?`ITestModule` 鏄墠缃０鏄庛€俙dispatch()` lambda 涓皟鐢?`mod->run(ctx)` 闇€瑕佸畬鏁寸被鍨嬨€?
**淇**锛歚TestEngine.cpp` 娣诲姞 `#include "tests/ITestModule.h"`銆?
#### 2.3.6 闄烽槺 F锛歋ysfsGpioDriver 瀹氫箟鍦?.cpp 涓紝鏃犳硶璺?TU make_unique

**鏆撮湶浣嶇疆**锛歚FalconPlatform.cpp` 缁戝畾 `SysfsGpioDriver` 鏃?
**闂**锛歚SysfsGpioDriver` 绫诲畾涔夊湪 `src/platforms/common/SysfsGpioDriver.cpp` 涓紝鍏朵粬 TU 鐪嬩笉鍒板畬鏁村畾涔夛紝`std::make_unique<SysfsGpioDriver>()` 缂栬瘧澶辫触銆?
**淇**锛氭彁鍙栫被澹版槑鍒?`include/platforms/common/SysfsGpioDriver.h`锛宍.cpp` 鍙繚鐣欐柟娉曞疄鐜般€俙FalconPlatform.cpp` 鍜?`BasePlatform.cpp`锛堝凡鍚堝苟鍒?PlatformFactory.cpp锛夊潎鍙?include 璇ュご鏂囦欢銆?
### 2.4 缂栬瘧楠岃瘉瀹炲綍

#### 2.4.1 null 骞冲彴锛坸86-64 鏈湴缂栬瘧锛?
**鐜**锛歐SL2, Ubuntu 22.04, GCC 11.4.0, CMake 3.22

**鍛戒护**锛?```bash
cd /home/gdh/FACTORY_GIT/factory_fw
PLATFORM=base ./build.sh
FACTORY_PLATFORM_JSON=platforms/base/platform.json \
  FACTORY_TESTS_JSON=platforms/base/tests.json \
  FACTORY_SELF_TEST=1 \
  ./build/base/factory_test
```

**杈撳嚭**锛?```
[SelfTest] triggering battery (sync)...
[Result] topic=15R status=SKIP detail=no battery driver
[SelfTest] triggering camera (async)...
[Result] topic=18R status=SKIP detail=no camera driver
[SelfTest] triggering sys (async)...
[Result] topic=26R status=FAIL detail=cannot read version
[SelfTest] done
```

**瑙ｈ**锛?- `battery` 鈫?`SKIP`锛坣ull 骞冲彴鏃?battery driver锛屾祴璇曟纭洖閫€鍒?skipped锛?- `camera` 鈫?`SKIP`锛坣ull 骞冲彴鏃?camera driver锛屽悓涓婏級
- `sys` 鈫?`FAIL`锛坄/etc/version` 鍦ㄥ紑鍙戠幆澧冧腑涓嶅瓨鍦紝ShellUtils 姝ｇ‘杩斿洖 fail锛?- **sync/async 鍒嗘敮鍧囧伐浣滄甯?*
- **dispatch 鈫?driver 鑾峰彇 鈫?娴嬭瘯鎵ц 鈫?result 涓婃姤 閾捐矾璐€?*

#### 2.4.2 falcon 骞冲彴锛圓RM64 浜ゅ弶缂栬瘧锛?
**鐜**锛歐SL2, `FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host`

**鍛戒护**锛?```bash
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
```

**杈撳嚭**锛?```
[100%] Linking CXX executable factory_test
[100%] Built target factory_test
[build_factory] Packaging firmware from /home/gdh/FACTORY_GIT/factory_fw/build/falcon
[build_factory] Done
```

**浜х墿楠岃瘉**锛?```bash
file build/falcon/factory_test
# ELF 64-bit LSB pie executable, ARM aarch64
```

**杩愯闄愬埗**锛欰RM 浜岃繘鍒舵棤娉曞湪 x86 涓绘満鐩存帴鎵ц锛堢己灏?aarch64 瑙ｉ噴鍣級锛岄渶鍦ㄧ洰鏍囨澘鎴?QEMU 涓繍琛屻€傝繖灞炰簬浜ゅ弶缂栬瘧鐨勯鏈熼檺鍒讹紝涓嶅奖鍝嶇紪璇戦獙璇佺殑鏈夋晥鎬с€?
### 2.5 浠ｇ爜鍙樻洿鎽樿

#### 2.5.1 鏂板鏂囦欢

| 鏂囦欢 | 璇存槑 |
|------|------|
| `platforms/falcon/FalconPlatform.cpp` | Falcon 骞冲彴鎬诲叆鍙ｏ紝registerDrivers() 缁戝畾 9 涓?driver |
| `platforms/falcon/drivers/*.h` (8 涓? | Driver 绫诲０鏄庯紙inline stub 瀹炵幇锛?|
| `platforms/falcon/drivers/*.cpp` (8 涓? | Driver 婧愭枃浠讹紙浠?`#include` 瀵瑰簲 .h锛?|
| `include/platforms/common/SysfsGpioDriver.h` | 閫氱敤 GPIO driver 澹版槑锛岃法骞冲彴澶嶇敤 |
| `platforms/base/platform.json` | null 骞冲彴鍩虹閰嶇疆 |
| `platforms/base/tests.json` | null 骞冲彴娴嬭瘯娉ㄥ唽琛紙4 椤癸紝鐢ㄤ簬鑷祴锛?|

#### 2.5.2 淇敼鏂囦欢

| 鏂囦欢 | 鍙樻洿 |
|------|------|
| `include/core/DriverRegistry.h` | `bind()` 绛惧彂鏀归€氱敤 Factory 妯℃澘 |
| `src/core/TestEngine.cpp` | 瀹炵幇 dispatch()銆乸ublishResult()銆乷nMqttMessage() |
| `src/main.cpp` | 鏀寔鐜鍙橀噺瑕嗙洊閰嶇疆璺緞 + 鑷祴妯″紡 |
| `src/platforms/common/PlatformFactory.cpp` | 鍚堝苟 BasePlatform锛岄槻 dead-strip |
| `src/platforms/common/SysfsGpioDriver.cpp` | 鎻愬彇澹版槑鍒?.h锛屽彧鐣欏疄鐜?|
| `src/platforms/common/CMakeLists.txt` | 绉婚櫎 BasePlatform.cpp |
| `platforms/falcon/CMakeLists.txt` | 鍔犲叆 FalconPlatform.cpp + include 璺緞 |
| `src/tests/*Test.cpp` (12 涓? | 浠庣┖妗╁～鍏呬负鏈€灏忓彲鎵ц楠ㄦ灦 |

### 2.6 璁捐鍐崇瓥璁板綍

#### 2.6.1 鍐崇瓥 1锛欴river stub 鐢?.h/.cpp 鍒嗙杩樻槸鍏ㄥ唴鑱旓紵

- **閫夐」 A**锛氬叏閮ㄥ唴鑱斿湪 `.h` 涓紝`.cpp` 涓嶉渶瑕?- **閫夐」 B**锛氬０鏄庡湪 `.h`锛屽疄鐜板湪 `.cpp`
- **閫夋嫨 B锛堜絾 stub 闃舵鎶婂疄鐜版斁鍦?.h锛?*锛氬洜涓烘湭鏉ョ湡瀹?driver 瀹炵幇浼氬緢澶э紙MPP 缂栫爜銆両2C 閫氫俊銆乂4L2 鎿嶄綔锛夛紝`.h` 鍙繚鐣欏０鏄庯紝`.cpp` 濉疄鐜般€傚綋鍓?stub 闃舵 `.cpp` 鍙湁 `#include "drivers/xxx.h"`锛岃繃娓℃湡鍚庨€愭鏇挎崲銆?
#### 2.6.2 鍐崇瓥 2锛歞ispatch 涓?`unique_ptr` 鈫?`shared_ptr` 鏄惁鍚堢悊锛?
- **闂**锛歚std::function` 瑕佹眰鍙鍒讹紝浣?`unique_ptr` 鍙兘绉诲姩
- **閫夐」 A**锛氫慨鏀?`AsyncTaskQueue::enqueue` 鎺ュ彈 `std::move_only_function`锛圕++23锛?- **閫夐」 B**锛氱敤 `shared_ptr` 鍖呰
- **閫夐」 C**锛氬湪鍏ラ槦鍓?release raw pointer锛屾墜鍔ㄧ鐞嗙敓鍛藉懆鏈?- **閫夋嫨 B**锛歚shared_ptr` 璇箟姝ｇ‘锛堟祴璇曟ā鍧楃殑鐢熷懡鍛ㄦ湡鐢?task 鍜岃皟鐢ㄦ柟鍏卞悓鎸佹湁锛夛紝浠ｇ爜绠€娲侊紝鏃犻渶 C++23銆傚敮涓€鐨勮交寰紑閿€鏄紩鐢ㄨ鏁帮紝鍙拷鐣ャ€?
#### 2.6.3 鍐崇瓥 3锛歱ublishResult 鍏堝啓 stdout 杩樻槸鐩存帴鎺?mosquitto锛?
- **閫夋嫨**锛氬厛鍐?stdout銆傚師鍥狅細
  1. MQTT 搴擄紙mosquitto锛夊皻鏈紩鍏ユ瀯寤虹郴缁?  2. stdout 杈撳嚭瓒冲楠岃瘉閾捐矾璐€?  3. 鍚庣画鏇挎崲涓?`mosquitto_publish` 鏄眬閮ㄦ敼鍔紝涓嶅奖鍝嶆灦鏋?
### 2.7 寰呭姙娓呭崟鏇存柊锛堥樁娈?2 缁撴潫鍚庯級

#### 2.7.1 宸茶В鍐筹紙浠?Phase 1 TODO 涓Щ闄わ級

| Phase 1 TODO | 瑙ｅ喅鏂瑰紡 |
|--------------|----------|
| `TestEngine::dispatch()` 绌烘々 | 宸插疄鐜板畬鏁?sync/async 鍒嗘敮 + 寮傚父瀹堝崼 |
| `TestEngine::publishResult()` 绌烘々 | 宸插疄鐜?stdout 杈撳嚭 + mutex 淇濇姢 |
| 12 涓祴璇曟ā鍧楃┖妗?| 宸插～鍏呮渶灏忛鏋?|
| `platforms/falcon/drivers/*.cpp` 绌烘々 | 宸插～鍏呮渶灏忓彲缂栬瘧 stub |
| falcon configure 楠岃瘉 | `PLATFORM=falcon ./build.sh` 缂栬瘧閫氳繃 |

#### 2.7.2 浠嶅緟鍚庣画闃舵

| # | 鍐呭 | 浼樺厛绾?| 璇存槑 |
|---|------|--------|------|
| G | mosquitto 绾跨▼瀹夊叏 | 涓?| 闇€寮曞叆 mosquitto 搴撳悗鎵嶈兘楠岃瘉 |
| J | 绗笁鏂逛緷璧栬幏鍙栨柟寮?| 鉁?| `third_party/nlohmann/json.hpp` 宸?vendored 鎻愪氦锛?.11.3 鍗曞ご鏂囦欢锛夛紝闅旂鏋勫缓楠岃瘉閫氳繃 |
| K | 闅旂鏋勫缓 | 鉁?| `cp -r factory_fw /tmp/fw_iso && cd /tmp/fw_iso && PLATFORM=base ./build.sh` 缂栬瘧閫氳繃 |
| M | MQTT 闆嗘垚 | 楂?| `onMqttMessage` 鐩墠浠?tests.json 鏌ユ壘 topic锛岄渶鎺ュ叆 mosquitto loop |
| N | BLE 妯″潡杩佺Щ | 涓?| `src/ble/*.cpp` 浠嶄负 stub |
| O | 骞冲彴 driver 鐪熷疄瀹炵幇 | 涓?| 8 涓?driver stub 闇€浠庢棫浠ｇ爜杩佺Щ鐪熷疄纭欢閫昏緫 |
| P | build_factory.sh 鎵撳寘 | 浣?| 鐩墠浠?echo锛岄渶鐢熸垚鍥轰欢闀滃儚 |

#### 2.7.3 涓婅疆 IWYU 閬楃暀锛堟湰杞‘璁ゅ凡瑙ｅ喅锛?
浠ヤ笅 3 鏉℃潵鑷笂杞瘎瀹★紝鏈疆楠岃瘉鏃剁‘璁ゅ凡钀藉湪浠ｇ爜涓紝浣嗕箣鍓?devlog 鏈褰曪細

| # | 闂 | 钀界偣 | 鐘舵€?|
|---|------|------|------|
| 1 | `running_` 搴旀敼 `std::atomic<bool>` | `include/core/TestEngine.h:34` | 鉁?`std::atomic<bool> running_{true};` |
| 2 | `TestEngine.cpp` 搴旀樉寮?`#include <thread>/<chrono>` | `src/core/TestEngine.cpp:9-10` | 鉁?宸插寘鍚?|
| 3 | `TestEngine.h` include 璺緞涓€鑷存€?| `include/core/TestEngine.h` | 鉁?缁熶竴浠?`include/` 鏍圭洰褰曞紩鐢?`core/DriverRegistry.h` |

### 2.8 璇勫妫€鏌ラ」

浠ヤ笅妫€鏌ラ」渚涢樁娈?2 璇勫浣跨敤锛?
- [ ] `FalconPlatform::registerDrivers()` 鏄惁缁戝畾浜嗗叏閮?9 涓帴鍙ｏ紵
- [ ] `DriverRegistry::bind()` 妯℃澘绛惧悕鏄惁姝ｇ‘澶勭悊 `unique_ptr<Derived>`锛?- [ ] `TestEngine::dispatch()` 鏄惁鍚屾椂鏀寔 sync 鍜?async 鍒嗘敮锛?- [ ] `TestEngine::dispatch()` 鏄惁鏈?`catch(...)` 寮傚父瀹堝崼锛?- [ ] `publishResult()` 鏄惁鏈?`mqttMutex_` 閿佷繚鎶わ紵
- [ ] null 骞冲彴杩愯鏃?`createPlatform("null")` 鏄惁杩斿洖闈炵┖锛?- [ ] falcon 骞冲彴浜ゅ弶缂栬瘧鏄惁閫氳繃锛?- [ ] 鑷祴杈撳嚭涓?sync/async 娴嬭瘯鏄惁閮戒骇鐢?`[Result]` 琛岋紵
- [ ] 12 涓祴璇曟ā鍧楁槸鍚﹂兘浣跨敤浜?`ctx.create<>()` 鎴?`ShellUtils`锛堣€岄潪鐩存帴 new 骞冲彴绫伙級锛?- [ ] `SysfsGpioDriver` 鏄惁鍙湁涓€浠藉畾涔夛紙鏃犻噸澶嶏級锛?
---

## 闃舵 3锛欴river 杩佺Щ + MQTT 鎺ュ叆 + 鏋勫缓绯荤粺琛ュ己

### 3.1 鐩爣

灏嗘棫浠ｇ爜锛團ACTORY_GIT 鏍圭洰褰曪級鐨勭敓浜х骇 driver 鐪熷疄閫昏緫杩佺Щ鍒?`factory_fw/platforms/falcon/drivers/`锛屾帴鍏?mosquitto 瀹炵幇 MQTT 鏀跺彂锛屼慨澶嶉摼鎺ラ『搴忚剢鎬э紝楠岃瘉鍙屽钩鍙扮紪璇戜笌闅旂鏋勫缓銆?
### 3.2 宸插畬鎴愪换鍔℃竻鍗?
| # | 浠诲姟 | 鐘舵€?| 鍏抽敭鏂囦欢 |
|---|------|------|----------|
| 1 | 鏋勫缓绯荤粺琛ュ己 鈥?mosquitto 棰勭紪璇戝簱 vendored | 鉁?| `factory_fw/third_party/mosquitto/` |
| 2 | 鏋勫缓绯荤粺琛ュ己 鈥?MPP / WiFi 搴?SDK 鎺㈡祴 | 鉁?| `platforms/falcon/CMakeLists.txt` |
| 3 | 閾炬帴椤哄簭鑴嗘€у交搴曚慨澶?| 鉁?| `platforms/falcon/CMakeLists.txt:39-44` |
| 4 | HallSwitchDriver 鐪熷疄閫昏緫杩佺Щ | 鉁?| `platforms/falcon/drivers/HallSwitchDriver.cpp` |
| 5 | Tmi8152MotorDriver 鐪熷疄閫昏緫杩佺Щ | 鉁?| `platforms/falcon/drivers/Tmi8152MotorDriver.cpp` |
| 6 | Rk3576WifiManager 鐪熷疄閫昏緫杩佺Щ | 鉁?| `platforms/falcon/drivers/Rk3576WifiManager.cpp` |
| 7 | Gc4663CameraDriver 楠ㄦ灦杩佺Щ | 鉁?| `platforms/falcon/drivers/Gc4663CameraDriver.cpp` |
| 8 | Om70x0xBatteryDriver 楠ㄦ灦杩佺Щ | 鉁?| `platforms/falcon/drivers/Om70x0xBatteryDriver.cpp` |
| 9 | RkMppEncoder 瀹屾暣鐢熶骇绾ц縼绉?| 鉁?| `platforms/falcon/drivers/RkMppEncoder.cpp` |
| 10 | MQTT 闆嗘垚 鈥?TestEngine 鎺ュ叆 mosquitto loop | 鉁?| `src/core/TestEngine.cpp` |
| 11 | I2cController 鎵╁睍锛坮eadRegister / writeRegister锛?| 鉁?| `src/control/I2cController.cpp` |
| 12 | build_factory.sh 鎵撳寘閫昏緫 | 鉁?| `platforms/falcon/build_factory.sh` |
| 13 | 鍙屽钩鍙扮紪璇?+ 闅旂鏋勫缓楠岃瘉 | 鉁?| `build/falcon/`, `build/base/`, `/tmp/fw_iso` |

### 3.3 鍏抽敭鍐崇瓥涓庨櫡闃?
#### 鍐崇瓥 1锛歮osquitto 鏉′欢缂栬瘧锛坄HAVE_MOSQUITTO`锛?
**闂**锛歚factory_core` 琚?null / falcon 鍙屽钩鍙板叡浜紝`TestEngine.cpp` 涓殑 mosquitto 浠ｇ爜涓嶈兘纭紪鐮佽繘 null 骞冲彴銆?
**鏂规**锛?- `factory_fw/CMakeLists.txt`锛歚if(TARGET_PLATFORM STREQUAL "falcon") add_compile_definitions(HAVE_MOSQUITTO) endif()`
- `TestEngine.cpp`锛歚#ifdef HAVE_MOSQUITTO` 鍖呰９鎵€鏈?mosquitto 璋冪敤
- null 骞冲彴缂栬瘧鏃惰嚜鍔ㄨ烦杩囷紝淇濇寔 stdout-only 鐨?`publishResult` 琛屼负

#### 鍐崇瓥 2锛歐iFiManager 涓?cJSON 鈫?nlohmann/json 鏇挎崲

**闂**锛氭棫浠ｇ爜 `BleWifiManager` 閲嶅害渚濊禆 `cJSON`锛屼絾 factory_fw 宸茬粺涓€浣跨敤 nlohmann/json銆?
**鏂规**锛氬唴鑱?`WifiCfgPack` / `WifiCfgUnpack` / `WifiCfgLength`锛屽叏閮ㄧ敤 `nlohmann::json::object` + `dump()` 瀹炵幇锛岄浂澶栭儴渚濊禆銆?
#### 鍐崇瓥 3锛氶摼鎺ラ『搴忓啀娆″彂浣滐紙factory_control锛?
**鏆撮湶浣嶇疆**锛歚Gc4663CameraDriver.cpp` 璋冪敤 `I2cController::readRegister` 鏃堕摼鎺ユ姤 undefined reference銆?
**鏍瑰洜**锛歚platform_falcon`锛坵hole-archive 寮哄埗鍏ㄨ繘锛夊紩鐢ㄤ簡 `factory_control` 涓殑绗﹀彿锛屼絾 `factory_control` 鎺掑湪 `platform_falcon` 鍓嶉潰锛岄潤鎬佸簱宸﹀埌鍙宠В鏋愭椂 `factory_control` 宸茶澶勭悊瀹屾瘯銆?
**淇**锛氬湪 `platforms/falcon/CMakeLists.txt` 涓０鏄庯細
```cmake
target_link_libraries(platform_falcon PUBLIC
    factory_platform_common
    factory_control
)
```
CMake 鑷姩鎶婅渚濊禆搴撴帓鍦?`platform_falcon` 鍚庨潰銆備笌璇勫鎰忚涓殑"澹版槑寮忎緷璧栨浛浠ｆ墜缁存姢閾炬帴椤哄簭"瀹屽叏涓€鑷淬€?
#### 鍐崇瓥 4锛歁PP Encoder 鐩存帴娌跨敤鏃т唬鐮?
**闂**锛歊K3576 涓?RV1126B 鐨?MPP API 鏄惁鍏煎锛?
**楠岃瘉**锛氭棫浠ｇ爜 `MppEncoder.cpp` 浣跨敤 `mpp_enc_cfg_set_s32` 鏂?API锛堥潪搴熷純鐨?struct-based API锛夛紝浜ゅ弶缂栬瘧閫氳繃锛宍librockchip_mpp.so` 鍦?SDK sysroot 涓瓨鍦ㄤ笖澶存枃浠惰矾寰勪竴鑷达紙`<rockchip/rk_mpi.h>`锛夈€?
**缁撹**锛欰PI 鍏煎锛岀洿鎺ヨ縼绉伙紝绫诲悕浠?`MppEncoder` 鏀逛负 `RkMppEncoder` 浠ラ€傞厤 `IEncoder` 鎺ュ彛銆?
### 3.4 缂栬瘧楠岃瘉瀹炲綍

#### falcon 骞冲彴锛圓RM64 浜ゅ弶缂栬瘧锛?
```bash
export FALCON_SDK=/home/gdh/falcon/Omni3576-sdk/buildroot/output/rockchip_rk3576_ipc/host
PLATFORM=falcon ./build.sh
# [100%] Built target factory_test
# [build_factory] Done: build/falcon/factory_firmware.tar.gz
```

浜х墿鍖呭惈锛歚factory_test` + `platform.json` + `tests.json` + `libmosquitto.so.1`

#### null 骞冲彴鍥炲綊锛坸86-64 鏈湴缂栬瘧锛?
```bash
PLATFORM=base ./build.sh
# [100%] Built target factory_test
```

#### 闅旂鏋勫缓楠岃瘉

```bash
rm -rf /tmp/fw_iso
cp -r factory_fw /tmp/fw_iso
rm -rf /tmp/fw_iso/build
cd /tmp/fw_iso && PLATFORM=base ./build.sh
# [100%] Built target factory_test
```

鏍稿績涓嶅彉寮忥紙"factory_fw 鏄彲鏁翠綋鎼蛋鐨勭嫭绔嬮」鐩牴"锛夋寔缁垚绔嬨€?
### 3.5 绗﹀彿瀛樻椿楠岃瘉

```bash
nm build/falcon/factory_test | grep -E 'HallSwitchDriver|Tmi8152MotorDriver|Rk3576WifiManager|RkMppEncoder|Gc4663CameraDriver|Om70x0xBatteryDriver'
# 鍏ㄩ儴绗﹀彿瀛樺湪锛圱/W 鏍囧織锛夛紝骞冲彴娉ㄥ唽娲荤潃
```

### 3.6 浠嶅緟鍚庣画闃舵

| # | 鍐呭 | 浼樺厛绾?| 璇存槑 |
|---|------|--------|------|
| A | LvglDisplayDriver 鐪熷疄瀹炵幇 | 涓?| 闇€寮曞叆 LVGL 婧愮爜锛?75 鏂囦欢/13MB锛夊埌 `third_party/lvgl/` |
| B | Rk3576Recorder 瀹屾暣瀹炵幇 | 楂?| 闇€杩佺Щ V4l2Recorder + AudioCapture + G711Encoder + Mp4Muxer锛堢害 2000 琛岋級|
| C | BLE 妯″潡杩佺Щ | 涓?| 5 涓?BLE 鏂囦欢锛孲DK 涓凡鏈?`libbluetooth.so` + `libdbus-1.so` |
| D | Camera/Battery RK3576 纭欢宸紓鏍稿 | 涓?| 涓婃澘鍚庢牳瀵?I2C 鎬荤嚎鍙枫€丱TP 璺緞銆佺數閲忚鍨嬪彿 |
| E | null 骞冲彴 MQTT 妯℃嫙鏀寔 | 浣?| 褰撳墠 null 骞冲彴鏈摼鎺?mosquitto锛屾湰鍦版棤娉曟ā鎷?MQTT 浜や簰 |

---

## 鏂囦欢绱㈠紩

### 璁捐鍐荤粨鏂囨。
- `factory_fw/docs/development/ARCHITECTURE.md` 鈥?鏋舵瀯璇存槑
- `factory_fw/docs/development/CODING_GUIDE.md` 鈥?缂栫爜瑙勮寖
- `docs/superpowers/specs/2026-06-03-factory-firmware-platform-design.md` 鈥?v1.3-final

### 鏍稿績鎺ュ彛
- `factory_fw/include/core/DriverRegistry.h` 鈥?閫氱敤 Factory 妯℃澘
- `factory_fw/include/core/TestEngine.h` 鈥?dispatch / publishResult / mosquitto 澹版槑
- `factory_fw/include/tests/ITestModule.h` 鈥?娴嬭瘯妯″潡鎺ュ彛

### 骞冲彴瀹炵幇
- `factory_fw/platforms/falcon/FalconPlatform.cpp` 鈥?骞冲彴鎬诲叆鍙?- `factory_fw/platforms/falcon/drivers/*.h` 鈥?9 涓?driver 澹版槑
- `factory_fw/platforms/falcon/platform.json` 鈥?纭欢璧勬簮閰嶇疆
- `factory_fw/platforms/falcon/tests.json` 鈥?娴嬭瘯椤规敞鍐岃〃

### 鏋勫缓鍏ュ彛
- `factory_fw/build.sh` 鈥?SCRIPT_DIR 閿氬畾
- `factory_fw/platforms/falcon/CMakeLists.txt` 鈥?骞冲彴鑷寘鍚?CMake + 澹版槑寮忎緷璧?- `factory_fw/platforms/base/CMakeLists.txt` 鈥?鐙珛/鑱氬悎涓ょ敤

---

## 阶段 3 命名和构建整理

### 调整内容

- 平台兜底实现从 `Null*` 统一改为 `Base*`。
- 基础平台从 `platforms/null` 改为 `platforms/base`，构建命令改为 `PLATFORM=base ./build.sh`。
- 通用 `src/hal` 改为 `src/control`，对应头文件目录改为 `include/control`，CMake 目标改为 `factory_control`。
- `factory_fw/src` 下多个模块级 CMake 合并为 `factory_fw/src/CMakeLists.txt` 单入口。
- 平台 CMake 不再逐个 `add_subdirectory(src/core)`、`src/control` 等目录，只引入统一 `src`。

### 当前边界

- `platforms/common/interface` 仍负责跨平台接口定义。
- `src/control` 只放平台无关的通用控制封装。
- `platforms/<platform>/drivers` 放真实平台 driver。
- 第三方 LVGL 内部的 `hal` 命名属于上游源码，不参与本轮重命名。