#include "npc_playercompanion.h"

enum HGruntType_t
{
	HGRUNT_GENERIC,
	HGRUNT_MEDIC,
	HGRUNT_ENGINEER
};

class CNPC_HGrunt : public CNPC_PlayerCompanion
{
	DECLARE_CLASS( CNPC_HGrunt, CNPC_PlayerCompanion );
	DECLARE_DATADESC();

public:
	virtual void	SelectModel();
	virtual void	Spawn();
	bool			IsMedic() { return m_iSquadRole == HGRUNT_MEDIC; }
	bool			IsEngineer() { return m_iSquadRole == HGRUNT_ENGINEER; }

	//---------------------------------
	// Behavior
	//---------------------------------
	bool			ShouldBehaviorSelectSchedule( CAI_BehaviorBase *pBehavior );
	void 			GatherConditions();
	void			PredictPlayerPush();
	void 			PrescheduleThink();
	void			BuildScheduleTestBits();

	bool			FInViewCone( CBaseEntity *pEntity );

	int				SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode );
	int				SelectSchedule();

	int 			SelectSchedulePriorityAction();
	int 			SelectScheduleHeal();
	int 			SelectScheduleRetrieveItem();
	int 			SelectScheduleNonCombat();
	int 			SelectScheduleCombat();
	bool			ShouldDeferToFollowBehavior();
	int 			TranslateSchedule( int scheduleType );

	bool			ShouldAcceptGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal );
	void			OnClearGoal( CAI_BehaviorBase *pBehavior, CAI_GoalEntity *pGoal );

	void 			StartTask( const Task_t *pTask );
	void 			RunTask( const Task_t *pTask );

	Activity		NPC_TranslateActivity( Activity eNewActivity );
	void 			HandleAnimEvent( animevent_t *pEvent );
	void			TaskFail( AI_TaskFailureCode_t code );

	void 			PickupItem( CBaseEntity *pItem );

	void 			SimpleUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value );

	bool			IgnorePlayerPushing( void );

	int				DrawDebugTextOverlays( void );

	virtual const char *SelectRandomExpressionForState( NPC_STATE state );
	virtual void	OnChangeRunningBehavior( CAI_BehaviorBase *pOldBehavior, CAI_BehaviorBase *pNewBehavior );
private:
	enum
	{
		TASK_HGRUNT_MEDIC_HEAL = BaseClass::NEXT_TASK
	};
	int m_iSquadRole;

	DEFINE_CUSTOM_AI;
};