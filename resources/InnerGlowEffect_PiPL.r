/*
	InnerGlowEffect_PiPL.r

	PiPL resource for Inner Glow Effect.

	After Effects reads this resource to discover the plug-in. If it is missing
	or malformed the effect simply will not appear in the Effect menu, with no
	error shown anywhere.

	!!  KEEP IN SYNC WITH GlobalSetup() IN src/InnerGlowEffect.cpp  !!

	AE_Effect_Global_OutFlags and AE_Effect_Global_OutFlags_2 below must be the
	exact numeric equivalent of the out_flags / out_flags2 that GlobalSetup()
	assigns. When they disagree, After Effects does not warn: it caches the PiPL
	values, then silently misbehaves (wrong bit depth, no smart render,
	threading crashes). The matching warning comment lives above GlobalSetup().

	AE_Effect_Global_OutFlags   0x02000000
	    PF_OutFlag_DEEP_COLOR_AWARE                 1L << 25   0x02000000

	    PF_OutFlag_PIX_INDEPENDENT is deliberately NOT set. The glow depends on
	    a pixel's distance from the nearest transparent pixel, so results are
	    not independent of their neighbours.

	AE_Effect_Global_OutFlags_2 0x08001400
	    PF_OutFlag2_SUPPORTS_SMART_RENDER           1L << 10   0x00000400
	    PF_OutFlag2_FLOAT_COLOR_AWARE               1L << 12   0x00001000
	    PF_OutFlag2_SUPPORTS_THREADED_RENDERING     1L << 27   0x08000000

	AE_Effect_Version 525825 is PF_VERSION(1, 0, 0, PF_Stage_RELEASE, 1):
	    (1 & 0x7)   << 19 = 524288
	    (3 & 0x3)   <<  9 =   1536
	    (1 & 0x1ff) <<  0 =      1
*/

#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include "AE_General.r"
#endif

resource 'PiPL' (16000) {
	{
		/* [1] */
		Kind {
			AEEffect
		},
		/* [2] */
		Name {
			"Inner Glow Effect"
		},
		/* [3] */
		Category {
			"Stylize"
		},
#ifdef AE_OS_WIN
	#if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
	#elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
	#endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		/* [5] */
		AE_PiPL_Version {
			2,
			0
		},
		/* [6] */
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		/* [7] */
		AE_Effect_Version {
			525825	/* 1.0 */
		},
		/* [8] */
		AE_Effect_Info_Flags {
			0
		},
		/* [9] */
		AE_Effect_Global_OutFlags {
			0x02000000
		},
		/* [10] */
		AE_Effect_Global_OutFlags_2 {
			0x08001400
		},
		/* [11] */
		AE_Effect_Match_Name {
			"DRAEXON InnerGlowEffect"
		},
		/* [12] */
		AE_Reserved_Info {
			8
		}
	}
};
