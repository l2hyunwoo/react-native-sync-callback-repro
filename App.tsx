import React, {useEffect, useState} from 'react';
import {NativeModules, StyleSheet, Text, View} from 'react-native';

const {Repro} = NativeModules;
const CALLBACKS_PER_CYCLE = 20000;

export default function App() {
  const [cycle] = useState<number>(() => Repro.nextCycle());
  useEffect(() => {
    // Native keeps these callbacks and never calls them.
    for (let i = 0; i < CALLBACKS_PER_CYCLE; i++) {
      Repro.hold(() => {});
    }
    const id = setTimeout(() => Repro.dropAndReload(), 500);
    return () => clearTimeout(id);
  }, []);
  return (
    <View style={styles.container}>
      <Text style={styles.title}>SyncCallback teardown repro</Text>
      <Text style={styles.cycle}>cycle {cycle}</Text>
      <Text>{CALLBACKS_PER_CYCLE} pending callbacks dropped, then reload</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {flex: 1, alignItems: 'center', justifyContent: 'center', gap: 12},
  title: {fontSize: 20, fontWeight: '600'},
  cycle: {fontSize: 48, fontVariant: ['tabular-nums']},
});
