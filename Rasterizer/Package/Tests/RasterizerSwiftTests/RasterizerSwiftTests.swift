import Testing

@testable import RasterizerObjC

@Test func testPath() async throws {
    let path = RAPath()
    path.move(to: 0, y: 0)
    path.line(to: 100, y: 100)
    
    #expect(path.bounds == CGRect(origin: .zero, size: CGSize(width: 100, height: 100)))
    
    // Write your test here and use APIs like `#expect(...)` to check expected conditions.
}

@Test func testView() async throws {
    let v = await RasterizerView()
    
    await #expect(v.useCG == false)
    
    // Write your test here and use APIs like `#expect(...)` to check expected conditions.
}

@Test func testDemoView() async throws {
    let v = await DemoView()
    
    await #expect(v.useCG == false)
    
    // Write your test here and use APIs like `#expect(...)` to check expected conditions.
}

@Test func testPDF() async throws {
    let scene = RAScene()
    let data = Data()
    let ctm = scene.addPdf(from: data, pageNumber: 0)
    
    #expect(ctm.isIdentity)
    
    // Write your test here and use APIs like `#expect(...)` to check expected conditions.
}

@Test func testFreetype() async throws {
    let font = RAFont(name: "HelveticaNeue-Medium")
    
    #expect(font != nil && font!.bounds.isEmpty)
    
    // Write your test here and use APIs like `#expect(...)` to check expected conditions.
}
