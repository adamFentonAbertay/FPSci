#include "TargetExperiment.h"
#include "TargetEntity.h"
#include "App.h"
#include <iostream>
#include <fstream>
#include <map>

void TargetExperiment::initPsychHelper()
{
	// Add conditions, one per one initial displacement value.
	// TODO: This must smartly iterate for every combination of an arbitrary number of arrays.
	// Iterate over the sessions here and add a config for each
	Array<Param> params = m_config.getTargetExpConditions(m_app->getCurrentSessionId());
	for (auto p : params) {
		// Define properties of psychophysical methods
		PsychophysicsDesignParameter psychParam;
		psychParam.mMeasuringMethod = PsychophysicsMethod::MethodOfConstantStimuli;
		// Can we remove this?
		psychParam.mIsDefault = false;
		// We need something in mStimLevels to run psychphysics...
		psychParam.mStimLevels.push_back(m_config.taskDuration);		// Shorter task is more difficult. However, we are currently doing unlimited time.
		psychParam.mMaxTrialCounts.push_back((int)p.val["trialCount"]);		// Get the trial count from the parameters
		p.add("session", m_app->getCurrentSessionId().c_str());
		m_psych.addCondition(p, psychParam);
	}

	// Update the logger w/ these conditions (IS THIS THE RIGHT PLACE TO DO THIS???)
	m_app->m_logger->addConditions(m_psych.mMeasurements);

	// call it once all conditions are defined.
	m_psych.chooseNextCondition();
}

void TargetExperiment::onInit() {
	// Initialize presentation states
	m_app->m_presentationState = PresentationState::initial;
	m_feedbackMessage = "Aim at the target and shoot!";
	
	m_config = m_app->m_experimentConfig;								// Setup config from app
	initPsychHelper();													// Initialize PsychHelper based on the configuration.
}

void TargetExperiment::onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface> >& surface)
{
	// seems like nothing necessary?
}

float TargetExperiment::randSign() {
	if (Random::common().uniform() > 0.5) {
		return 1;
	}
	else {
		return -1;
	}
}

void TargetExperiment::initTargetAnimation() {
	// initialize target location based on the initial displacement values
	// Not reference: we don't want it to change after the first call.
	static const Point3 initialSpawnPos = m_app->activeCamera()->frame().translation + Point3(-m_app->m_spawnDistance, 0.0f, 0.0f);
	m_app->m_motionFrame = CFrame::fromXYZYPRDegrees(initialSpawnPos.x, initialSpawnPos.y, initialSpawnPos.z, 0.0f, 0.0f, 0.0f);
	m_app->m_motionFrame.lookAt(Point3(0.0f, 0.0f, -1.0f)); // look at the -z direction

	// In task state, spawn a test target. Otherwise spawn a target at straight ahead.
	if (m_app->m_presentationState == PresentationState::task) {
		m_speed = G3D::Random::common().uniform(m_psych.getParam().val["minSpeed"], m_psych.getParam().val["maxSpeed"]);
		float rot_pitch = randSign() * Random::common().uniform(m_psych.getParam().val["minEccV"], m_psych.getParam().val["maxEccV"]);
		float rot_yaw = randSign() * Random::common().uniform(m_psych.getParam().val["minEccH"], m_psych.getParam().val["maxEccH"]);
		m_app->m_motionFrame = (m_app->m_motionFrame.toMatrix4() * Matrix4::pitchDegrees(rot_pitch)).approxCoordinateFrame();
		m_app->m_motionFrame = (m_app->m_motionFrame.toMatrix4() * Matrix4::yawDegrees(rot_yaw)).approxCoordinateFrame();

		// Apply roll rotation by a random amount (random angle in degree from 0 to 360)
		float randomAngleDegree = G3D::Random::common().uniform() * 360;
		m_app->m_motionFrame = (m_app->m_motionFrame.toMatrix4() * Matrix4::rollDegrees(randomAngleDegree)).approxCoordinateFrame();
	}
	else {
		m_speed = 0;
	}

	// Full health for the target
	m_app->m_targetHealth = 1.f;
	// reset click counter
	m_clickCount = 0;

	// Don't reset the view. Wait for the subject to center on the ready target.
	//m_app->resetView();
}


void TargetExperiment::processResponse()
{
	m_taskExecutionTime = m_app->timer.getTime();
	m_response = (m_app->m_targetHealth <= 0) ? 1 : 0; // 1 means success, 0 means failure.
	recordTrialResponse(); // NOTE: we need record response first before processing it with PsychHelper.
	m_psych.processResponse(m_response); // process response.
	/*if (m_app->m_expConfig.expMode == "training") {
		m_feedbackMessage = format("%d ms!", (int)(m_taskExecutionTime * 1000));
	}*/
}

void TargetExperiment::updatePresentationState()
{
	// This updates presentation state and also deals with data collection when each trial ends.
	PresentationState currentState = m_app->m_presentationState;
	PresentationState newState;
	float stateElapsedTime = m_app->timer.getTime();

	newState = currentState;

	if (currentState == PresentationState::initial)
	{
		if (!m_app->m_buttonUp)
		{
			m_feedbackMessage = "";
			newState = PresentationState::feedback;
		}
	}
	else if (currentState == PresentationState::ready)
	{
		if (stateElapsedTime > m_config.readyDuration)
		{
			m_lastMotionChangeAt = 0;
			m_app->m_targetColor = Color3::green().pow(2.0f);
			newState = PresentationState::task;
		}
	}
	else if (currentState == PresentationState::task)
	{
		if ((stateElapsedTime > m_config.taskDuration) || (m_app->m_targetHealth <= 0) || (m_clickCount == m_config.maxClicks))
		{
			m_taskEndTime = System::time();
			processResponse();
			m_app->clearTargets(); // clear all remaining targets
			m_app->m_targetColor = Color3::red().pow(2.0f);
			newState = PresentationState::feedback;
		}
	}
	else if (currentState == PresentationState::feedback)
	{
		if ((stateElapsedTime > m_config.feedbackDuration) && (m_app->m_targetHealth <= 0))
		{
			if (m_psych.isComplete()) {
				m_feedbackMessage = "Session complete. Thanks!";
				newState = PresentationState::complete;
				m_app->markSessionComplete(String(m_psych.getParam().str["session"]));			// Add this session to user's completed sessions
				m_app->userSaveButtonPress();	// Press the save button for the user...
				Array<String> remaining = m_app->updateSessionDropDown();
				if (remaining.size() > 0) {
					//String nextSess = remaining.randomElement();				// Choose a random next session
					//m_app->updateSession(nextSess);								// Update the session
				}
			}
			else {
				m_feedbackMessage = "";
				m_psych.chooseNextCondition();
				newState = PresentationState::ready;
			}
		}
	}
	else {
		newState = currentState;
	}

	if (currentState != newState)
	{ // handle state transition.
		m_app->timer.startTimer();
		if (newState == PresentationState::task) {
			m_taskStartTime = System::time();
		}
		m_app->m_presentationState = newState;
		//If we switched to task, call initTargetAnimation to handle new trial
		if ((newState == PresentationState::task) || (newState == PresentationState::feedback)) {
			initTargetAnimation();
		}
	}
}

void TargetExperiment::onSimulation(RealTime rdt, SimTime sdt, SimTime idt)
{
	// 1. Update presentation state and send task performance to psychophysics library.
	updatePresentationState();

	// 2. Check if motionChange is required (happens only during 'task' state with a designated level of chance).
	if (m_app->m_presentationState == PresentationState::task)
	{
		if (m_app->timer.getTime() > m_lastMotionChangeAt + m_psych.getParam().val["motionChangePeriod"])
		{
			// If yes, rotate target coordinate frame by random (0~360) angle in roll direction
			m_lastMotionChangeAt = m_app->timer.getTime();
			float randomAngleDegree = G3D::Random::common().uniform() * 360;
			m_app->m_motionFrame = (m_app->m_motionFrame.toMatrix4() * Matrix4::rollDegrees(randomAngleDegree)).approxCoordinateFrame();
		}
	}

	// 3. update target location (happens only during 'task' and 'feedback' states).
	if (m_app->m_presentationState == PresentationState::task)
	{
		float rotationAngleDegree = (float)rdt * m_speed;

		// Attempts to bound the target within visible space.
		//float currentYaw;
		//float ignore;
		//m_motionFrame.getXYZYPRDegrees(ignore, ignore, ignore, currentYaw, ignore, ignore);
		//static int reverse = 1;
		//const float change = abs(currentYaw - rotationAngleDegree);
		//// If we are headed in the wrong direction, reverse the yaw.
		//if (change > m_yawBound && change > abs(currentYaw)) {
		//    reverse *= -1;
		//}
		//m_motionFrame = (m_motionFrame.toMatrix4() * Matrix4::yawDegrees(-rotationAngleDegree * reverse)).approxCoordinateFrame();

		m_app->m_motionFrame = (m_app->m_motionFrame.toMatrix4() * Matrix4::yawDegrees(-rotationAngleDegree)).approxCoordinateFrame();

	}

	// 4. Clear m_TargetArray. Append an object with m_targetLocation if necessary ('task' and 'feedback' states).
	Point3 t_pos = m_app->m_motionFrame.pointToWorldSpace(Point3(0, 0, -m_app->m_targetDistance));


	if (m_app->m_targetHealth > 0.f) {
	//if (m_app->m_presentationState == PresentationState::task) {
			// Don't spawn a new target every frame
		if (m_app->m_targetArray.size() == 0) {
			m_app->spawnTarget(t_pos, m_psych.getParam().val["visualSize"], false, m_app->m_targetColor);
		}
		else {
			// TODO: don't hardcode assumption of a single target
			m_app->m_targetArray[0]->setFrame(t_pos);
		}
	}

	// record target trajectories, view direction trajectories, and mouse motion.
	if (m_app->m_presentationState == PresentationState::task)
	{
		accumulateTrajectories();
	}
}

void TargetExperiment::onUserInput(UserInput* ui)
{
	// nothing here, handled in App::onUserInput
}

void TargetExperiment::onGraphics2D(RenderDevice* rd)
{
	const float scale = rd->viewport().width() / 1920.0f;
	rd->setBlendFunc(RenderDevice::BLEND_SRC_ALPHA, RenderDevice::BLEND_ONE_MINUS_SRC_ALPHA);

	// Reticle
	Draw::rect2D(
		(m_app->m_reticleTexture->rect2DBounds() * scale - m_app->m_reticleTexture->vector2Bounds() * scale / 2.0f) / 2.0f + rd->viewport().wh() / 2.0f,
		rd, Color3::green(), m_app->m_reticleTexture
	);

	// TODO: Feels like the following variables should be members of TargetExperiment:
	// m_renderHud, m_hudTexture, m_reticleTexture, ...
	if (m_app->m_renderHud && !m_app->m_emergencyTurbo) {
		const Point2 hudCenter(rd->viewport().width() / 2.0f, m_app->m_hudTexture->height() * scale * 0.48f);
		Draw::rect2D((m_app->m_hudTexture->rect2DBounds() * scale - m_app->m_hudTexture->vector2Bounds() * scale / 2.0f) * 0.8f + hudCenter, rd, Color3::white(), m_app->m_hudTexture);
		m_app->m_hudFont->draw2D(rd, "1:36", hudCenter - Vector2(80, 0) * scale, scale * 20, Color3::white(), Color4::clear(), GFont::XALIGN_RIGHT, GFont::YALIGN_CENTER);
		m_app->m_hudFont->draw2D(rd, "86%", hudCenter + Vector2(7, -1), scale * 30, Color3::white(), Color4::clear(), GFont::XALIGN_CENTER, GFont::YALIGN_CENTER);
		m_app->m_hudFont->draw2D(rd, "2080", hudCenter + Vector2(125, 0) * scale, scale * 20, Color3::white(), Color4::clear(), GFont::XALIGN_RIGHT, GFont::YALIGN_CENTER);
	}

	if (!m_feedbackMessage.empty()) {
		m_app->m_outputFont->draw2D(rd, m_feedbackMessage.c_str(),
			(Point2((float)m_app->window()->width() / 2 - 40, (float)m_app->window()->height() / 2 + 20) * scale).floor(), floor(20.0f * scale), Color3::yellow());
	}
}

void TargetExperiment::recordTrialResponse()
{
	String sess = String(m_psych.mMeasurements[m_psych.mCurrentConditionIndex].getParam().str["session"]);

	// Trials table. Record trial start time, end time, and task completion time.
	std::vector<std::string> trialValues = {
		std::to_string(m_psych.mCurrentConditionIndex),
		addQuotes(sess.c_str()),
		addQuotes(m_config.getSessionConfigById(sess)->expMode.c_str()),
		std::to_string(m_taskStartTime),
		std::to_string(m_taskEndTime),
		std::to_string(m_taskExecutionTime)
	};
	m_app->m_logger->recordTrialResponse(trialValues);

	// Target_Trajectory table. Write down the recorded target trajectories.
	m_app->m_logger->recordTargetTrajectory(m_targetTrajectory);
	m_targetTrajectory.clear();

	// Player_Action table. Write down the recorded player actions.
	m_app->m_logger->recordPlayerActions(m_playerActions);
	m_playerActions.clear();
}

void TargetExperiment::accumulateTrajectories()
{
	// recording target trajectories
	Point3 targetPosition = m_app->getTargetPosition();
	std::vector<std::string> targetTrajectoryValues = {
		std::to_string(System::time()),
		std::to_string(targetPosition.x),
		std::to_string(targetPosition.y),
		std::to_string(targetPosition.z),
	};
	m_targetTrajectory.push_back(targetTrajectoryValues);

	// recording view direction trajectories
	accumulatePlayerAction("aim");
}

void TargetExperiment::accumulatePlayerAction(String action)
{
	// recording target trajectories
	Point2 dir = m_app->getViewDirection();
	std::vector<std::string> playerActionValues = {
		std::to_string(System::time()),
		// TODO: make this nicer
		format("'%s'",action.c_str()).c_str(),
		std::to_string(dir.x),
		std::to_string(dir.y),
	};
	m_playerActions.push_back(playerActionValues);
}
