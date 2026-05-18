// Import the functions you need from the SDKs you need
import { initializeApp } from "firebase/app";
import { getAnalytics } from "firebase/analytics";
// TODO: Add SDKs for Firebase products that you want to use
// https://firebase.google.com/docs/web/setup#available-libraries

// Your web app's Firebase configuration
// For Firebase JS SDK v7.20.0 and later, measurementId is optional
const firebaseConfig = {
  apiKey: "AIzaSyDrmSoZA86dYSZ0eDKLdC_zzGQspTqLoI0",
  authDomain: "firealarm-8587f.firebaseapp.com",
  projectId: "firealarm-8587f",
  storageBucket: "firealarm-8587f.firebasestorage.app",
  messagingSenderId: "1056047994812",
  appId: "1:1056047994812:web:29dfdeab0e4f4c8d0dfdb9",
  measurementId: "G-EZXRK7C8D9"
};

// Initialize Firebase
const app = initializeApp(firebaseConfig);
const analytics = getAnalytics(app);