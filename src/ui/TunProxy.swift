/* 前台开关与代理设置，主题及布局取自 Tunailscale。 */
import SwiftUI

/* 复用现有海沫绿主题。 */
enum TPTheme {
    static let primary = Color(red: 15/255, green: 118/255, blue: 110/255)
    static let accent = Color(red: 20/255, green: 184/255, blue: 166/255)
    static let background = Color(red: 9/255, green: 10/255, blue: 10/255)
    static let surface = Color(red: 39/255, green: 39/255, blue: 42/255)
    static let text = Color(red: 244/255, green: 244/255, blue: 245/255)
    static let secondary = Color(red: 161/255, green: 161/255, blue: 170/255)
}

/* 管理独立转发核心的状态与启停。 */
@MainActor final class TPModel: ObservableObject {
    @Published var host = UserDefaults.standard.string(forKey: "host") ?? ""
    @Published var port = UserDefaults.standard.string(forKey: "port") ?? "7897"
    @Published var running = false
    @Published var busy = false
    @Published var iface = "—"
    @Published var error: String?
    private var pending = false

    /* 对齐前后台的真实状态。 */
    func sync() {
        guard !pending, !busy else { return }
        pending = true
        DispatchQueue.global(qos: .utility).async {
            /* 从后台读取状态。 */
            let state = TPSpawn.rqst("status")
            DispatchQueue.main.async {
                /* 发布实际运行状态。 */
                guard !self.busy else { self.pending = false; return }
                self.running = state?["running"] as? Bool ?? false
                self.iface = state?["interface"] as? String ?? "—"
                if self.running {
                    self.host = state?["host"] as? String ?? self.host
                    self.port = (state?["port"] as? NSNumber)?.stringValue ?? self.port
                }
                self.pending = false
            }
        }
    }

    /* 用同一入口切换开关和主按钮。 */
    func tggl() {
        guard !busy else { return }
        let address = host.trimmingCharacters(in: .whitespacesAndNewlines)
        guard running || (!address.isEmpty && UInt16(port).map({ $0 > 0 }) == true) else {
            error = "请输入Tailscale地址和有效端口"
            return
        }
        busy = true
        let stopping = running
        let number = port
        let binary = Bundle.main.path(forResource: "tunproxyd", ofType: nil)
        UserDefaults.standard.set(address, forKey: "host")
        UserDefaults.standard.set(number, forKey: "port")
        DispatchQueue.global(qos: .utility).async {
            /* 执行启停并等待核心响应。 */
            var message: String?
            if stopping {
                if TPSpawn.rqst("stop") == nil { message = "无法停止后台，请重试" }
            } else if let binary = binary {
                let code = TPSpawn.spwn(binary, host: address, port: number)
                if code != 0 { message = "启动失败：\(String(cString: strerror(code)))" }
            } else { message = "安装包缺少转发核心" }
            var state: [AnyHashable: Any]?
            if message == nil {
                for _ in 0..<40 {
                    state = TPSpawn.rqst("status")
                    let active = state?["running"] as? Bool ?? false
                    if stopping ? state == nil : active { break }
                    Thread.sleep(forTimeInterval: 0.1)
                }
                let active = state?["running"] as? Bool ?? false
                if stopping ? state != nil : !active {
                    let log = (try? String(contentsOfFile: "/tmp/tunproxy.log", encoding: .utf8)) ?? ""
                    message = "\(stopping ? "停止" : "启动")未完成\n\(log.suffix(1600))"
                }
            }
            DispatchQueue.main.async {
                /* 以后台响应更新界面。 */
                self.busy = false
                self.pending = false
                self.error = message
                self.sync()
            }
        }
    }
}

/* 复用原主页开关、电源区和单层设置。 */
struct TPHome: View {
    @StateObject private var model = TPModel()
    @Environment(\.scenePhase) private var phase
    @State private var settings = false
    @ScaledMetric(relativeTo: .title2) private var statusSize = 26
    private let timer = Timer.publish(every: 2, on: .main, in: .common).autoconnect()

    var body: some View {
        ZStack {
            TPTheme.background.ignoresSafeArea()
            GeometryReader { geometry in
                ScrollView {
                    VStack(spacing: 24) {
                        HStack(spacing: 14) {
                            Toggle("代理", isOn: Binding(get: { model.running }, set: { _ in model.tggl() }))
                                .labelsHidden().tint(TPTheme.accent).disabled(model.busy)
                            Text("TunProxy").font(.body.weight(.semibold))
                            Spacer()
                            Button { settings = true } label: {
                                Image(systemName: "gearshape.fill").font(.title2).frame(minWidth: 44, minHeight: 44)
                            }.accessibilityLabel("代理设置")
                        }.padding(.top, 12)
                        Spacer(minLength: 16)
                        Image(systemName: "power").font(.system(size: 68, weight: .bold))
                            .foregroundColor(model.running ? TPTheme.accent : TPTheme.secondary.opacity(0.5))
                            .frame(width: 104, height: 104)
                            .background(Circle().fill(model.running ? TPTheme.accent.opacity(0.12) : Color.clear))
                        Text(model.busy ? "正在处理" : model.running ? "已开启" : "未开启")
                            .font(.system(size: statusSize, weight: .bold))
                        Text("\(model.host):\(model.port)").font(.system(.footnote, design: .monospaced))
                            .foregroundColor(TPTheme.secondary).textSelection(.enabled)
                            .fixedSize(horizontal: false, vertical: true)
                        Button { model.tggl() } label: {
                            Group {
                                if model.busy { ProgressView().tint(.white).accessibilityLabel("正在处理") }
                                else { Text(model.running ? "关闭代理" : "开启代理") }
                            }.font(.body.weight(.semibold)).foregroundColor(.white)
                                .frame(maxWidth: .infinity, minHeight: 52)
                                .background(model.running ? TPTheme.surface : TPTheme.primary).cornerRadius(8)
                        }.disabled(model.busy)
                        if model.running { Text(model.iface).font(.system(.footnote, design: .monospaced)).foregroundColor(TPTheme.secondary) }
                        Spacer(minLength: 16)
                    }.padding(.horizontal, 16).padding(.bottom, 24)
                        .frame(maxWidth: 480, minHeight: geometry.size.height)
                        .frame(maxWidth: .infinity)
                }
            }
        }.foregroundColor(TPTheme.text)
            .sheet(isPresented: $settings) {
                NavigationView {
                    Form {
                        TextField("Tailscale地址", text: $model.host).textInputAutocapitalization(.never).autocorrectionDisabled()
                        TextField("端口", text: $model.port).keyboardType(.numberPad)
                    }.disabled(model.running || model.busy)
                        .navigationTitle("代理设置")
                        .toolbar { ToolbarItem(placement: .confirmationAction) { Button("完成") { settings = false } } }
                }.navigationViewStyle(.stack).tint(TPTheme.accent)
            }
            .alert("TunProxy", isPresented: Binding(get: { model.error != nil }, set: { if !$0 { model.error = nil } })) {
                Button("确定", role: .cancel) { model.error = nil }
            } message: { Text(model.error ?? "") }
            .onAppear { model.sync() }
            .onChange(of: phase) { value in if value == .active { model.sync() } }
            .onReceive(timer) { _ in if phase == .active { model.sync() } }
    }
}

/* 独立应用入口。 */
@main struct TunProxy: App {
    var body: some Scene { WindowGroup { TPHome() } }
}
