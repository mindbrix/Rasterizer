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
