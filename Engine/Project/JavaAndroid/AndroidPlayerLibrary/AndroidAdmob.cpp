// Copyright(c) 2017 POLYGONTEK
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
// http ://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "Precompiled.h"
#include "Application.h"
#include "AndroidAdmob.h"

RewardBasedVideoAd rewardBasedVideoAd;
jobject RewardBasedVideoAd::sActivity;
extern __thread JNIEnv *g_env;

void RewardBasedVideoAd::RegisterLuaModule(LuaCpp::State *state) {

	state->RegisterModule("admob", [](LuaCpp::Module &module) {
		LuaCpp::Selector _RewardBasedVideoAd = module["RewardBasedVideoAd"];

		_RewardBasedVideoAd.SetObj(rewardBasedVideoAd);
		_RewardBasedVideoAd.AddObjMembers(rewardBasedVideoAd,
			"request", &RewardBasedVideoAd::Request,
			"is_ready", &RewardBasedVideoAd::IsReady,
			"present", &RewardBasedVideoAd::Present);
	});
}

void RewardBasedVideoAd::Request(const char *unitID, const char *testDevices) {

	JNIEnv *env = g_env;
	jclass androidPlayerClass = env->FindClass("com/AndroidPlayer/AndroidPlayer");
	jmethodID loadRewardedVideoAdMid = env->GetMethodID(androidPlayerClass, "loadRewardedVideoAd", "()V");
	env->CallVoidMethod(sActivity, loadRewardedVideoAdMid);

		//GADRequest *request = [GADRequest request];

		//BE1::StrArray testDeviceList;
		//BE1::SplitStringIntoList(testDeviceList, testDevices, " ");

		//if (testDeviceList.Count() > 0) {
		//	NSString *nsTestDevices[256];
		//	for (int i = 0; i < testDeviceList.Count(); i++) {
		//		nsTestDevices[i] = [[NSString alloc] initWithBytes:testDeviceList[i].c_str() length : testDeviceList[i].Length() encoding : NSUTF8StringEncoding];
		//	}

		//	request.testDevices = [NSArray arrayWithObjects : nsTestDevices count : testDeviceList.Count()];
		//}

		//if (!unitID || !unitID[0]) {
		//	unitID = "ca-app-pub-3940256099942544/1712485313";
		//}
		//NSString *nsUnitID = [[NSString alloc] initWithBytes:unitID length : strlen(unitID) encoding : NSUTF8StringEncoding];
		//// Set up event notification
		//// Required to set the delegate prior to loading an ad.
		//[GADRewardBasedVideoAd sharedInstance].delegate = viewController;
		//[[GADRewardBasedVideoAd sharedInstance] loadRequest:request
		//	withAdUnitID : nsUnitID];
}

bool RewardBasedVideoAd::IsReady() const {
	JNIEnv *env = g_env;
	jclass androidPlayerClass = env->FindClass("com/AndroidPlayer/AndroidPlayer");
	jmethodID isLoadedRewardedVideoAdMid = env->GetMethodID(androidPlayerClass, "isLoadedRewardedVideoAd", "()Z");
	jboolean ret = env->CallBooleanMethod(sActivity, isLoadedRewardedVideoAdMid);
	return ret;
	//	return[[GADRewardBasedVideoAd sharedInstance] isReady];
}

void RewardBasedVideoAd::Present() {
	JNIEnv *env = g_env;
	jclass androidPlayerClass = env->FindClass("com/AndroidPlayer/AndroidPlayer");
	jmethodID showRewardedVideoAdMid = env->GetMethodID(androidPlayerClass, "showRewardedVideoAd", "()V");
	env->CallVoidMethod(sActivity, showRewardedVideoAdMid);
//	[[GADRewardBasedVideoAd sharedInstance] presentFromRootViewController:viewController];
}


