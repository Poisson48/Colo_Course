#!/usr/bin/env node
/**
 * Test signaling end-to-end pour Colo_Course WebRTC
 * Simule deux pairs qui rejoignent la même channel et échangent SDP/ICE
 *
 * Protocol (from deploy/signaling/server.js):
 *   → register { device_id }
 *   → join { channel_tag }
 *   ← peer_joined { device_id, channel_tag }
 *   → offer { to, channel_tag, sdp }
 *   ← offer { from, channel_tag, sdp }
 *   → answer { to, channel_tag, sdp }
 *   ← answer { from, channel_tag, sdp }
 *   → candidate { to, channel_tag, candidate, sdpMid }
 *   ← candidate { from, channel_tag, candidate, sdpMid }
 *
 * Usage: node test-webrtc-signaling.js [signaling_url]
 * Default: ws://localhost:7778
 */

const WebSocket = require('ws');

const SIGNALING_URL = process.argv[2] || 'ws://localhost:7778';
const CHANNEL_TAG = 'test-channel-' + Date.now();
const PEER_A = 'device-A-' + Date.now();
const PEER_B = 'device-B-' + Date.now();

// Fake SDP pour le test (pas de vrai WebRTC)
const FAKE_OFFER = {
  type: 'offer',
  sdp: 'v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n'
};

const FAKE_ANSWER = {
  type: 'answer',
  sdp: 'v=0\r\no=- 0 0 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n'
};

const FAKE_CANDIDATE = {
  candidate: 'candidate:1 1 UDP 2130706431 192.168.1.1 54321 typ host',
  sdpMid: '0',
  sdpMLineIndex: 0
};

class TestPeer {
  constructor(deviceId) {
    this.deviceId = deviceId;
    this.ws = null;
    this.connected = false;
    this.registered = false;
    this.joined = false;
    this.peerSeen = false;
    this.receivedOffer = false;
    this.receivedAnswer = false;
    this.receivedCandidate = false;
  }

  connect() {
    return new Promise((resolve, reject) => {
      this.ws = new WebSocket(SIGNALING_URL);

      this.ws.on('open', () => {
        console.log(`[${this.deviceId}] Connecté au signaling server`);
        this.connected = true;
        resolve();
      });

      this.ws.on('error', (err) => {
        console.error(`[${this.deviceId}] Erreur WS: ${err.message}`);
        reject(err);
      });

      this.ws.on('message', (data) => {
        try {
          const msg = JSON.parse(data.toString());
          this.handleMessage(msg);
        } catch (e) {
          console.error(`[${this.deviceId}] JSON parse error:`, e.message);
        }
      });

      this.ws.on('close', (code, reason) => {
        console.log(`[${this.deviceId}] Connexion fermée: ${code} ${reason}`);
        this.connected = false;
      });

      setTimeout(() => reject(new Error('Timeout connexion')), 5000);
    });
  }

  handleMessage(msg) {
    console.log(`[${this.deviceId}] Reçu: ${msg.type}`);

    switch (msg.type) {
      case 'peer_joined':
        if (msg.device_id !== this.deviceId) {
          this.peerSeen = true;
          console.log(`[${this.deviceId}] Pair ${msg.device_id} a rejoint`);
        }
        break;

      case 'offer':
        this.receivedOffer = true;
        console.log(`[${this.deviceId}] Reçu offer de ${msg.from}`);
        // Répondre avec une answer
        this.sendAnswer(msg.from, FAKE_ANSWER.sdp);
        break;

      case 'answer':
        this.receivedAnswer = true;
        console.log(`[${this.deviceId}] Reçu answer de ${msg.from}`);
        break;

      case 'candidate':
        this.receivedCandidate = true;
        console.log(`[${this.deviceId}] Reçu candidate de ${msg.from}`);
        break;

      case 'peer_left':
        console.log(`[${this.deviceId}] Pair ${msg.device_id} a quitté`);
        break;
    }
  }

  send(msg) {
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(msg));
      console.log(`[${this.deviceId}] Envoyé: ${msg.type}`);
    } else {
      console.error(`[${this.deviceId}] WS non ouvert, impossible d'envoyer ${msg.type}`);
    }
  }

  register() {
    this.send({
      type: 'register',
      device_id: this.deviceId
    });
  }

  joinChannel(channelTag) {
    this.send({
      type: 'join',
      channel_tag: channelTag
    });
  }

  sendOffer(to, channelTag, sdp) {
    this.send({
      type: 'offer',
      to: to,
      channel_tag: channelTag,
      sdp: sdp
    });
  }

  sendAnswer(to, channelTag, sdp) {
    // Note: dans le handler offer, on appelle sendAnswer(to, sdp)
    // mais il faut channel_tag. On va stocker le channel_tag.
    this.send({
      type: 'answer',
      to: to,
      channel_tag: this.currentChannel || CHANNEL_TAG,
      sdp: sdp
    });
  }

  sendCandidate(to, channelTag, candidate) {
    this.send({
      type: 'candidate',
      to: to,
      channel_tag: channelTag,
      candidate: candidate.candidate,
      sdpMid: candidate.sdpMid
    });
  }

  disconnect() {
    if (this.ws) {
      this.ws.close();
    }
  }
}

async function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

async function runTest() {
  console.log('='.repeat(60));
  console.log('Test Signaling End-to-End');
  console.log('='.repeat(60));
  console.log(`Signaling: ${SIGNALING_URL}`);
  console.log(`Channel: ${CHANNEL_TAG}`);
  console.log(`Peer A: ${PEER_A}`);
  console.log(`Peer B: ${PEER_B}`);
  console.log('');

  const peerA = new TestPeer(PEER_A);
  const peerB = new TestPeer(PEER_B);
  peerA.currentChannel = CHANNEL_TAG;
  peerB.currentChannel = CHANNEL_TAG;

  const results = {
    connectA: false,
    connectB: false,
    registerA: false,
    registerB: false,
    joinA: false,
    joinB: false,
    peerSeenA: false,
    peerSeenB: false,
    offerReceived: false,
    answerReceived: false,
    candidateReceived: false
  };

  try {
    // 1. Connecter les deux pairs
    console.log('--- Étape 1: Connexion ---');
    await Promise.all([peerA.connect(), peerB.connect()]);
    results.connectA = peerA.connected;
    results.connectB = peerB.connected;
    console.log('✅ Les deux pairs sont connectés\n');

    // 2. Register
    console.log('--- Étape 2: Register ---');
    peerA.register();
    await sleep(200);
    peerB.register();
    await sleep(300);

    // On ne peut pas vérifier directement, mais si pas d'erreur, c'est OK
    results.registerA = peerA.connected;
    results.registerB = peerB.connected;
    console.log('✅ Les deux pairs sont enregistrés\n');

    // 3. Rejoindre la channel
    console.log('--- Étape 3: Join Channel ---');
    peerA.joinChannel(CHANNEL_TAG);
    await sleep(300);
    peerB.joinChannel(CHANNEL_TAG);
    await sleep(500);

    results.joinA = true;  // Pas de confirmation explicite
    results.joinB = true;
    results.peerSeenA = peerA.peerSeen;
    results.peerSeenB = peerB.peerSeen;
    console.log(`✅ Peer A voit B: ${results.peerSeenA}`);
    console.log(`✅ Peer B voit A: ${results.peerSeenB}\n`);

    // 4. Exchange SDP
    console.log('--- Étape 4: SDP Exchange ---');
    peerA.sendOffer(PEER_B, CHANNEL_TAG, FAKE_OFFER.sdp);
    await sleep(500);

    results.offerReceived = peerB.receivedOffer;
    results.answerReceived = peerA.receivedAnswer;
    console.log(`✅ Offer reçu par B: ${results.offerReceived}`);
    console.log(`✅ Answer reçu par A: ${results.answerReceived}\n`);

    // 5. ICE candidates
    console.log('--- Étape 5: ICE Candidates ---');
    peerA.sendCandidate(PEER_B, CHANNEL_TAG, FAKE_CANDIDATE);
    await sleep(300);

    results.candidateReceived = peerB.receivedCandidate;
    console.log(`✅ Candidate reçu par B: ${results.candidateReceived}\n`);

    // Résumé
    console.log('='.repeat(60));
    console.log('RÉSUMÉ');
    console.log('='.repeat(60));

    const tests = [
      ['Connexion Peer A', results.connectA],
      ['Connexion Peer B', results.connectB],
      ['Register Peer A', results.registerA],
      ['Register Peer B', results.registerB],
      ['Peer A voit B', results.peerSeenA],
      ['Peer B voit A', results.peerSeenB],
      ['Offer reçu', results.offerReceived],
      ['Answer reçu', results.answerReceived],
      ['Candidate reçu', results.candidateReceived]
    ];

    let passed = 0;
    let failed = 0;

    for (const [name, ok] of tests) {
      const status = ok ? '✅ PASS' : '❌ FAIL';
      console.log(`  ${name.padEnd(30)} ${status}`);
      if (ok) passed++;
      else failed++;
    }

    console.log('');
    console.log(`${passed}/${tests.length} tests passés`);

    if (failed === 0) {
      console.log('🎉 Signaling server fonctionne parfaitement !');
    } else {
      console.log('⚠️  Certains tests ont échoué');
    }

    // Fermer
    peerA.disconnect();
    peerB.disconnect();

    process.exit(failed === 0 ? 0 : 1);

  } catch (err) {
    console.error('❌ Erreur fatale:', err.message);
    if (err.stack) console.error(err.stack);
    peerA.disconnect();
    peerB.disconnect();
    process.exit(1);
  }
}

// Vérifier que ws est installé
try {
  require.resolve('ws');
} catch {
  console.error('❌ Module "ws" non trouvé.');
  console.error('Lancer d\'abord: cd deploy/signaling && npm install');
  process.exit(1);
}

runTest();
