import Testing

@testable import RasterizerObjC

@Test func testPath() async throws {
    let path = RAPath()
    path.move(to: 0, y: 0)
    path.line(to: 100, y: 100)
    
    #expect(path.bounds == CGRect(origin: .zero, size: CGSize(width: 100, height: 100)))
}

@Test func testView() async throws {
    let v = await RasterizerView()
    
    await #expect(v.useCG == false)
}

@Test func testDemoView() async throws {
    let v = await DemoView()
    
    await #expect(v.useCG == false)
}

@Test func testPDF() async throws {
    let scene = RAScene()
    let data = Data()
    let ctm = scene.addPdf(from: data, pageNumber: 0)
    
    #expect(ctm.isIdentity)
}

@Test func testFreetype() async throws {
    guard let font = RAFont(name: "HelveticaNeue-Medium") else {
        #expect(Bool(false))
        return
    }
    #expect(!font.bounds.isNull)
}
