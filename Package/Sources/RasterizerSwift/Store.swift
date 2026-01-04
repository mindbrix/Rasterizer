//
//  Store.swift
//  RasterizerSwift
//
//  Created by Nigel Barber on 02/01/2026.
//

import Foundation
import OrderedCollections


class Store {
    typealias KeyType = String
    typealias ValueType = Any
    typealias DictType = OrderedDictionary<KeyType, ValueType>
    
    func getValue(key: KeyType) -> ValueType? {
        dict[key]
    }
    func setValue(key: KeyType, value: ValueType?) {
        dict[key] = value
    }
    func merge(_ other: DictType) {
        for (key, value) in other {
            dict[key] = value
        }
    }
    var dict: DictType = [:]
}

extension Store.DictType {
    func getValue(keys: [Store.KeyType]) -> Store.ValueType? {
        if keys.isEmpty {
            return nil
        } else if keys.count == 1 {
            return self[keys[0]]
        } else if let dict = self[keys[0]] as? Store.DictType {
            return dict.getValue(keys: Array(keys[1..<keys.count]))
        } else {
            return nil
        }
    }
}
